/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2026 zh-pipeline
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// zh-pipeline @feature 2026-11-17 Searches on a navigation mesh, see NavMeshQuery.h.

#ifndef NAVMESH_STANDALONE
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine
#endif

#include "GameLogic/NavMeshQuery.h"

#include <math.h>
#include <queue>

namespace NavQuery
{

using NavBuild::Mesh;
using NavBuild::Tri;

namespace
{
	const float BIG = 1.0e30f;

	inline Pt pointOf( const Mesh& mesh, int v )
	{
		Pt p;
		p.x = (float)mesh.verts[v].x;
		p.y = (float)mesh.verts[v].y;
		return p;
	}
	inline float cross( const Pt& o, const Pt& a, const Pt& b )
	{
		return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
	}
	inline float dot( const Pt& o, const Pt& a, const Pt& b )
	{
		return (a.x - o.x) * (b.x - o.x) + (a.y - o.y) * (b.y - o.y);
	}
	inline float dist( const Pt& a, const Pt& b )
	{
		float dx = b.x - a.x, dy = b.y - a.y;
		return (float)sqrt(dx*dx + dy*dy);
	}
	Pt closestOnSegment( const Pt& p, const Pt& a, const Pt& b )
	{
		float len2 = (b.x-a.x)*(b.x-a.x) + (b.y-a.y)*(b.y-a.y);
		float t = len2 > 0.0f ? dot(a, b, p) / len2 : 0.0f;
		if (t < 0.0f) t = 0.0f;
		if (t > 1.0f) t = 1.0f;
		Pt q;
		q.x = a.x + (b.x - a.x) * t;
		q.y = a.y + (b.y - a.y) * t;
		return q;
	}
	inline float distToSegment( const Pt& p, const Pt& a, const Pt& b )
	{
		return dist(p, closestOnSegment(p, a, b));
	}
	inline bool passable( unsigned mask, unsigned char label )
	{
		return label < 32 && ((mask >> label) & 1u) != 0;
	}
	inline int backIndex( const Mesh& mesh, int nb, int t )
	{
		const Tri& n = mesh.tris[nb];
		return n.n[0] == t ? 0 : (n.n[1] == t ? 1 : 2);
	}

	struct Portal
	{
		Pt left, right;
	};

	// A portal is the segment crossed on the way into a triangle, as the traveller sees it. Entering across edge e
	// (v[e] -> v[e+1], inside on its left), v[e] is on our left.

	/// The funnel ("simple stupid funnel algorithm"): the shortest line through a row of portals.
	/// Mesh vertices are lattice points, so collinear cases give cross products of exactly zero; a zero counts as
	/// "still inside the funnel" (a start that lies on a portal's line must not turn that portal's end into a corner).
	/// 'from', if given, gets for each point of the string the portal it is an end of (-1 for the start, the number
	/// of portals for the goal).
	void funnel( const std::vector<Portal>& portals, const Pt& start, const Pt& goal, std::vector<Pt>& out, std::vector<int> *from = 0 )
	{
		out.clear();
		out.push_back(start);
		if (from)
		{
			from->clear();
			from->push_back(-1);
		}
		Pt apex = start, left = start, right = start;
		int leftIndex = 0, rightIndex = 0;
		int n = (int)portals.size();
		int guard = n * n + 64;
		for (int i = 0; i <= n && --guard > 0; ++i)
		{
			Pt newLeft = (i < n) ? portals[i].left : goal;
			Pt newRight = (i < n) ? portals[i].right : goal;

			// Right side.
			if (cross(apex, right, newRight) >= 0.0f)
			{
				bool apexIsRight = (apex.x == right.x && apex.y == right.y);
				if (apexIsRight || cross(apex, left, newRight) <= 0.0f)
				{
					right = newRight;
					rightIndex = i;
				}
				else
				{
					// The right side crossed over the left: the left point is a corner of the path.
					out.push_back(left);
					if (from)
						from->push_back(leftIndex);
					apex = left;
					i = leftIndex;
					left = apex; right = apex;
					rightIndex = leftIndex;
					continue;
				}
			}
			// Left side.
			if (cross(apex, left, newLeft) <= 0.0f)
			{
				bool apexIsLeft = (apex.x == left.x && apex.y == left.y);
				if (apexIsLeft || cross(apex, right, newLeft) >= 0.0f)
				{
					left = newLeft;
					leftIndex = i;
				}
				else
				{
					out.push_back(right);
					if (from)
						from->push_back(rightIndex);
					apex = right;
					i = rightIndex;
					left = apex; right = apex;
					leftIndex = rightIndex;
					continue;
				}
			}
		}
		if (out.back().x != goal.x || out.back().y != goal.y)
		{
			out.push_back(goal);
			if (from)
				from->push_back(n);
		}
	}

	float lengthOf( const std::vector<Pt>& pts )
	{
		float len = 0.0f;
		for (size_t k = 0; k + 1 < pts.size(); ++k)
			len += dist(pts[k], pts[k+1]);
		return len;
	}

	struct OpenNode
	{
		float f;
		unsigned seq;
		int node;
		float reach;
	};
	struct OpenOrder
	{
		bool operator()( const OpenNode& a, const OpenNode& b ) const
		{
			if (a.f != b.f)
				return a.f > b.f;
			return a.seq > b.seq;
		}
	};
}

//-------------------------------------------------------------------------------------------------
int locate( const Mesh& mesh, Pt p, unsigned mask )
{
	// @todo a bucket grid; the meshes are a few thousand triangles, so this is not yet worth it.
	// "On the edge" means within a thousandth of a cell of it: the cross product is that distance times the edge's
	// length, so the allowance grows with the edge (a path may run along a wall hundreds of cells long).
	const float EPS = 1.0e-3f;
	int any = -1;
	for (size_t t = 0; t < mesh.tris.size(); ++t)
	{
		const Tri& tr = mesh.tris[t];
		Pt a = pointOf(mesh, tr.v[0]), b = pointOf(mesh, tr.v[1]), c = pointOf(mesh, tr.v[2]);
		if (cross(a, b, p) >= -EPS * dist(a, b) && cross(b, c, p) >= -EPS * dist(b, c) && cross(c, a, p) >= -EPS * dist(c, a))
		{
			if (passable(mask, tr.label))
				return (int)t;
			if (any < 0)
				any = (int)t;
		}
	}
	return any;
}

//-------------------------------------------------------------------------------------------------
bool nearestPassable( const Mesh& mesh, Pt p, unsigned mask, float maxDist, const int *component, int wantComponent,
											Pt& out, int& tri )
{
	float best = maxDist;
	bool found = false;
	for (size_t t = 0; t < mesh.tris.size(); ++t)
	{
		const Tri& tr = mesh.tris[t];
		if (!passable(mask, tr.label) || (component != 0 && component[t] != wantComponent))
			continue;
		Pt a = pointOf(mesh, tr.v[0]), b = pointOf(mesh, tr.v[1]), c = pointOf(mesh, tr.v[2]);
		if (cross(a, b, p) >= 0.0f && cross(b, c, p) >= 0.0f && cross(c, a, p) >= 0.0f)
		{
			out = p;
			tri = (int)t;
			return true;
		}
		Pt q[3];
		q[0] = closestOnSegment(p, a, b);
		q[1] = closestOnSegment(p, b, c);
		q[2] = closestOnSegment(p, c, a);
		for (int i = 0; i < 3; ++i)
		{
			float d = dist(p, q[i]);
			if (d < best)
			{
				best = d;
				// A hair inside the triangle, so that what is found here is on the mesh for every later test.
				Pt mid;
				mid.x = (a.x + b.x + c.x) / 3.0f;
				mid.y = (a.y + b.y + c.y) / 3.0f;
				float toMid = dist(q[i], mid);
				float nudge = toMid > 0.0f ? 0.01f / toMid : 0.0f;
				if (nudge > 1.0f)
					nudge = 1.0f;
				out.x = q[i].x + (mid.x - q[i].x) * nudge;
				out.y = q[i].y + (mid.y - q[i].y) * nudge;
				tri = (int)t;
				found = true;
			}
		}
	}
	return found;
}

//-------------------------------------------------------------------------------------------------
namespace
{
/// From a point s in triangle t to a point e in triangle te: does the straight line stay on passable ground?
bool walkClear( const Mesh& mesh, Pt s, int t, Pt e, int te, unsigned mask )
{
	float len = dist(s, e);
	float tol = 1.0e-3f * (len > 1.0f ? len : 1.0f);
	int guard = (int)mesh.tris.size() + 8;
	while (--guard > 0)
	{
		if (t == te)
			return true;
		const Tri& tr = mesh.tris[t];
		Pt v[3];
		v[0] = pointOf(mesh, tr.v[0]);
		v[1] = pointOf(mesh, tr.v[1]);
		v[2] = pointOf(mesh, tr.v[2]);
		// Already in the triangle the end is in (te is only one of those that hold it)?
		if (cross(v[0], v[1], e) >= -tol && cross(v[1], v[2], e) >= -tol && cross(v[2], v[0], e) >= -tol)
			return true;
		// The edge the segment leaves by: the end is outside it, and the segment passes between its two points
		// (the edge runs right to left as the traveller sees it).
		int out = -1;
		for (int i = 0; i < 3 && out < 0; ++i)
		{
			const Pt& right = v[i];
			const Pt& left = v[(i+1)%3];
			if (cross(right, left, e) < 0.0f && cross(s, e, right) <= tol && cross(s, e, left) >= -tol)
				out = i;
		}
		if (out < 0 || tr.n[out] < 0 || !passable(mask, mesh.tris[tr.n[out]].label))
			return false;
		t = tr.n[out];
	}
	return false;
}

/// The nearest point of one triangle to p (p itself if inside), and how far.
float nearestOnTri( const Mesh& mesh, const Tri& tr, const Pt& p, Pt& out )
{
	Pt a = pointOf(mesh, tr.v[0]), b = pointOf(mesh, tr.v[1]), c = pointOf(mesh, tr.v[2]);
	if (cross(a, b, p) >= 0.0f && cross(b, c, p) >= 0.0f && cross(c, a, p) >= 0.0f)
	{
		out = p;
		return 0.0f;
	}
	Pt q[3];
	q[0] = closestOnSegment(p, a, b);
	q[1] = closestOnSegment(p, b, c);
	q[2] = closestOnSegment(p, c, a);
	float best = 1.0e30f;
	for (int i = 0; i < 3; ++i)
	{
		float d = dist(p, q[i]);
		if (d < best)
		{
			best = d;
			// a hair inside, as nearestPassable does
			Pt mid;
			mid.x = (a.x + b.x + c.x) / 3.0f;
			mid.y = (a.y + b.y + c.y) / 3.0f;
			float toMid = dist(q[i], mid);
			float nudge = toMid > 0.0f ? 0.01f / toMid : 0.0f;
			if (nudge > 1.0f)
				nudge = 1.0f;
			out.x = q[i].x + (mid.x - q[i].x) * nudge;
			out.y = q[i].y + (mid.y - q[i].y) * nudge;
		}
	}
	return best;
}
}	// namespace

//-------------------------------------------------------------------------------------------------
bool segmentClear( const Mesh& mesh, Pt a, Pt b, unsigned mask, float snap )
{
	Pt s, e;
	int t = -1, te = -1;
	if (!nearestPassable(mesh, a, mask, snap, 0, -1, s, t) || !nearestPassable(mesh, b, mask, snap, 0, -1, e, te))
		return false;
	return walkClear(mesh, s, t, e, te, mask);
}

//-------------------------------------------------------------------------------------------------
void TriGrid::build( const Mesh& mesh, int cellSize )
{
	cell = cellSize > 0 ? cellSize : 8;
	w = mesh.width / cell + 1;
	h = mesh.height / cell + 1;
	std::vector<int> count(w * h + 1, 0);
	// two passes: count, then fill (triangles end up in each bucket in index order)
	for (int pass = 0; pass < 2; ++pass)
	{
		for (size_t t = 0; t < mesh.tris.size(); ++t)
		{
			const Tri& tr = mesh.tris[t];
			int x0 = mesh.verts[tr.v[0]].x, x1 = x0, y0 = mesh.verts[tr.v[0]].y, y1 = y0;
			for (int k = 1; k < 3; ++k)
			{
				int vx = mesh.verts[tr.v[k]].x, vy = mesh.verts[tr.v[k]].y;
				if (vx < x0) x0 = vx;
				if (vx > x1) x1 = vx;
				if (vy < y0) y0 = vy;
				if (vy > y1) y1 = vy;
			}
			int bx0 = x0 / cell, bx1 = x1 / cell, by0 = y0 / cell, by1 = y1 / cell;
			if (bx1 >= w) bx1 = w - 1;
			if (by1 >= h) by1 = h - 1;
			for (int by = by0; by <= by1; ++by)
			{
				for (int bx = bx0; bx <= bx1; ++bx)
				{
					if (pass == 0)
						++count[by * w + bx];
					else
						tris[start[by * w + bx] + count[by * w + bx]++] = (int)t;
				}
			}
		}
		if (pass == 0)
		{
			start.assign(w * h + 1, 0);
			for (int b = 0; b < w * h; ++b)
				start[b+1] = start[b] + count[b];
			tris.assign(start[w * h], 0);
			count.assign(w * h + 1, 0);
		}
	}
}

//-------------------------------------------------------------------------------------------------
bool nearestPassable( const Mesh& mesh, const TriGrid& grid, Pt p, unsigned mask, float maxDist, Pt& out, int& tri )
{
	if (grid.w <= 0 || grid.h <= 0)
		return nearestPassable(mesh, p, mask, maxDist, 0, -1, out, tri);
	int bx0 = (int)floor((p.x - maxDist) / (float)grid.cell), bx1 = (int)floor((p.x + maxDist) / (float)grid.cell);
	int by0 = (int)floor((p.y - maxDist) / (float)grid.cell), by1 = (int)floor((p.y + maxDist) / (float)grid.cell);
	if (bx0 < 0) bx0 = 0;
	if (by0 < 0) by0 = 0;
	if (bx1 >= grid.w) bx1 = grid.w - 1;
	if (by1 >= grid.h) by1 = grid.h - 1;
	float best = maxDist;
	int bestTri = -1;
	bool found = false;
	for (int by = by0; by <= by1; ++by)
	{
		for (int bx = bx0; bx <= bx1; ++bx)
		{
			int b = by * grid.w + bx;
			for (int k = grid.start[b]; k < grid.start[b+1]; ++k)
			{
				int t = grid.tris[k];
				const Tri& tr = mesh.tris[t];
				if (!passable(mask, tr.label))
					continue;
				Pt q;
				float d = nearestOnTri(mesh, tr, p, q);
				// the same triangle may be met in several buckets: the lowest index wins a tie, as in the plain scan
				if (d < best || (found && d == best && t < bestTri) || (!found && d <= best && d == 0.0f))
				{
					best = d;
					bestTri = t;
					out = q;
					found = true;
				}
			}
		}
	}
	tri = bestTri;
	return found;
}

//-------------------------------------------------------------------------------------------------
bool segmentClear( const Mesh& mesh, const TriGrid& grid, Pt a, Pt b, unsigned mask, float snap )
{
	Pt s, e;
	int t = -1, te = -1;
	if (!nearestPassable(mesh, grid, a, mask, snap, s, t) || !nearestPassable(mesh, grid, b, mask, snap, e, te))
		return false;
	return walkClear(mesh, s, t, e, te, mask);
}

//-------------------------------------------------------------------------------------------------
// The atlas: the ground's mesh and one mesh per bridge, joined where their ground meets.

namespace
{
	const unsigned GROUND_MASK = 1u << NavBuild::SHEET_GROUND;
	const unsigned SHEET_MASK = (1u << NavBuild::SHEET_GROUND) | (1u << NavBuild::SHEET_DECK);
	const float MIN_PORTAL = 1.0e-3f;	// two edges that share less than this are not joined

	inline int sheetOfTri( const Atlas& atlas, int g )
	{
		int s = (int)atlas.sheets.size() - 1;
		while (s > 0 && atlas.triBase[s] > g)
			--s;
		return s;
	}
	inline const Tri& triOf( const Atlas& atlas, int g, int& sheet )
	{
		sheet = sheetOfTri(atlas, g);
		return atlas.sheets[sheet].mesh->tris[g - atlas.triBase[sheet]];
	}
	inline Pt pointOf( const Sheet& sheet, int v )
	{
		Pt p;
		p.x = (float)(sheet.mesh->verts[v].x + sheet.ox);
		p.y = (float)(sheet.mesh->verts[v].y + sheet.oy);
		return p;
	}
	inline int totalTris( const Atlas& atlas )
	{
		return atlas.sheets.empty() ? 0 : atlas.triBase.back() + (int)atlas.sheets.back().mesh->tris.size();
	}

	// Search nodes. "Triangle g entered through its edge e" is g*3 + e. Past those come two per link:
	// 2j = the link's triangle A entered from B, 2j + 1 = its triangle B entered from A.
	struct Nodes
	{
		const Atlas& atlas;
		int edgeNodes;
		Nodes( const Atlas& a ) : atlas(a), edgeNodes(totalTris(a) * 3) {}
		int count() const { return edgeNodes + (int)atlas.links.size() * 2; }
		bool isLink( int node ) const { return node >= edgeNodes; }
		int linkOf( int node ) const { return (node - edgeNodes) / 2; }
		bool intoA( int node ) const { return ((node - edgeNodes) & 1) == 0; }
		int tri( int node ) const
		{
			if (!isLink(node))
				return node / 3;
			const Link& l = atlas.links[linkOf(node)];
			return intoA(node) ? l.triA : l.triB;
		}
		/// The segment crossed on the way into the node's triangle, as the traveller sees it.
		Portal portal( int node ) const
		{
			Portal p;
			if (!isLink(node))
			{
				int sheet;
				const Tri& ct = triOf(atlas, node / 3, sheet);
				int ce = node % 3;
				p.left = pointOf(atlas.sheets[sheet], ct.v[ce]);
				p.right = pointOf(atlas.sheets[sheet], ct.v[(ce+1)%3]);
				return p;
			}
			// a->b runs along A's edge with A on its left: going into A, a is on our left.
			const Link& l = atlas.links[linkOf(node)];
			if (intoA(node))
			{
				p.left = l.a;
				p.right = l.b;
			}
			else
			{
				p.left = l.b;
				p.right = l.a;
			}
			return p;
		}
	};

	struct Seat
	{
		int sheet, tri;
	};

	// ---- Corridors that turn back across the line they came in by.
	// Where a bridge's deck opens onto ground, a triangle of that ground can have two neighbours across one edge: the
	// deck, and ground of another sheet that lies under the deck. A corridor may come in from the one and leave to
	// the other: the path touches that line and turns back. The funnel cannot pull a string through a strip that is
	// folded over itself, so the strip is unfolded first: everything after such a turn is mirrored in the line (these
	// lines are grid lines, so a mirror is a sign and an offset per axis, exact in floating point), the string is
	// pulled through the flat strip, and its points are mirrored back.
	struct Mirror
	{
		float sx, ox, sy, oy;		// x' = sx*x + ox, y' = sy*y + oy, with sx, sy = +1 or -1
		Mirror() : sx(1.0f), ox(0.0f), sy(1.0f), oy(0.0f) {}
		bool flips() const { return sx * sy < 0.0f; }
		Pt apply( const Pt& p ) const
		{
			Pt q;
			q.x = sx * p.x + ox;
			q.y = sy * p.y + oy;
			return q;
		}
		Pt undo( const Pt& p ) const
		{
			Pt q;
			q.x = sx * (p.x - ox);
			q.y = sy * (p.y - oy);
			return q;
		}
	};

	/// Does a corridor that came in across 'in' and leaves across 'out' turn back across one grid line?
	inline bool turnsBack( const Portal& in, const Portal& out )
	{
		if (cross(in.left, in.right, out.left) != 0.0f || cross(in.left, in.right, out.right) != 0.0f)
			return false;
		if (in.left.x != in.right.x && in.left.y != in.right.y)
			return false;
		return (in.right.x - in.left.x) * (out.right.x - out.left.x) + (in.right.y - in.left.y) * (out.right.y - out.left.y) < 0.0f;
	}

	/// The mirror for what follows portal 'out', given the mirror up to it.
	inline Mirror mirrorAfter( const Mirror& m, const Portal& in, const Portal& out )
	{
		if (!turnsBack(in, out))
			return m;
		Mirror r = m;
		if (out.left.x == out.right.x)
		{
			// mirror in x = c, then m: sx*(2c - x) + ox
			r.ox = m.ox + m.sx * 2.0f * out.left.x;
			r.sx = -m.sx;
		}
		else
		{
			r.oy = m.oy + m.sy * 2.0f * out.left.y;
			r.sy = -m.sy;
		}
		return r;
	}

	inline Portal mirrored( const Mirror& m, const Portal& p )
	{
		Portal q;
		q.left = m.apply(m.flips() ? p.right : p.left);
		q.right = m.apply(m.flips() ? p.left : p.right);
		return q;
	}

	inline bool sameMirror( const Mirror& a, const Mirror& b )
	{
		return a.sx == b.sx && a.sy == b.sy && a.ox == b.ox && a.oy == b.oy;
	}

	/// The portals of a corridor laid out flat, and the mirror each was laid out with. False if nothing is mirrored.
	bool unfold( const std::vector<Portal>& portals, std::vector<Portal>& flat, std::vector<Mirror>& mirrors )
	{
		flat.clear();
		mirrors.clear();
		Mirror m;
		bool any = false;
		for (size_t i = 0; i < portals.size(); ++i)
		{
			if (i > 0)
			{
				Mirror next = mirrorAfter(m, portals[i-1], portals[i]);
				if (!sameMirror(next, m))
					any = true;
				m = next;
			}
			mirrors.push_back(m);
			flat.push_back(mirrored(m, portals[i]));
		}
		return any;
	}

	/// The string from start through the corridor's portals to 'target', a point past the last of them (on a
	/// further portal 'next', if given): pulled on the flat strip, given back where it really is. Where the corridor
	/// turns back across a line the string is straight on the flat strip and has a corner in reality: that point,
	/// where it touches the line, is added.
	void pull( const std::vector<Portal>& portals, const std::vector<Portal>& flat, const std::vector<Mirror>& mirrors, bool folded,
						 const Portal *next, const Pt& start, const Pt& target, std::vector<Pt>& out, std::vector<Pt>& scratch,
						 std::vector<int>& from )
	{
		Mirror last = mirrors.empty() ? Mirror() : mirrors.back();
		if (next && !portals.empty())
		{
			Mirror m = mirrorAfter(last, portals.back(), *next);
			if (!sameMirror(m, last))
				folded = true;
			last = m;
		}
		if (!folded)
		{
			funnel(portals, start, target, out);
			return;
		}
		// (The portal the target lies on is not itself a portal of the pull: the target is a point.)
		funnel(flat, start, last.apply(target), scratch, &from);
		out.clear();
		int n = (int)mirrors.size();
		for (size_t k = 0; k < scratch.size(); ++k)
		{
			int j = from[k];
			if (k > 0)
			{
				// The turns between the last point and this one: portals from[k-1]+1 .. j-1 are crossed on this
				// straight stretch; so is portal j itself unless this point is one of its ends (a corner), and the
				// line the target is on when the corridor turns back there.
				for (int f = from[k-1] + 1; f <= j && f <= n; ++f)
				{
					if (f < 1)
						continue;
					Mirror before = mirrors[f-1];
					Mirror after = (f < n) ? mirrors[f] : last;
					if (sameMirror(before, after))
						continue;
					// The line, on the flat strip: that of the portal before the turn.
					const Portal& line = flat[f-1];
					const Pt& p = scratch[k-1];
					const Pt& q = scratch[k];
					float dp = cross(line.left, line.right, p), dq = cross(line.left, line.right, q);
					Pt x = q;
					if (dp != dq && ((dp <= 0.0f && dq >= 0.0f) || (dp >= 0.0f && dq <= 0.0f)))
					{
						float t = dp / (dp - dq);
						x.x = p.x + (q.x - p.x) * t;
						x.y = p.y + (q.y - p.y) * t;
					}
					out.push_back(before.undo(x));
				}
			}
			if (j < 0)
				out.push_back(scratch[k]);
			else
				out.push_back(j < n ? mirrors[j].undo(scratch[k]) : last.undo(scratch[k]));
		}
		// A touch that falls on a corner gave the same point twice.
		size_t w = 1;
		for (size_t r = 1; r < out.size(); ++r)
		{
			if (r + 1 < out.size() && out[r].x == out[w-1].x && out[r].y == out[w-1].y)
				continue;
			out[w++] = out[r];
		}
		out.resize(w);
	}

	/// The pulled string with what the engine needs to know about it: which sheet each stretch is on, where it gets
	/// on and off a bridge. 'chain' is the corridor (search nodes from the start outward), 'pts' its string.
	void describe( const Nodes& nodes, int startTri, const std::vector<int>& chain, const std::vector<Pt>& pts, AtlasPath& out )
	{
		const Atlas& atlas = nodes.atlas;
		out.pts.clear();
		out.sheet.clear();
		out.deck.clear();
		out.inserted.clear();
		int sheet;
		const Tri& st = triOf(atlas, startTri, sheet);
		int curSheet = sheet;
		unsigned char curDeck = st.label == NavBuild::SHEET_DECK ? 1 : 0;
		if (pts.size() < 2)
		{
			for (size_t k = 0; k < pts.size(); ++k)
			{
				out.pts.push_back(pts[k]);
				out.sheet.push_back(curSheet);
				out.deck.push_back(curDeck);
				out.inserted.push_back(0);
			}
			return;
		}

		// Where the string crosses each portal at which something changes: (segment, how far along it).
		struct Change
		{
			int seg;
			float t;
			int sheet;
			unsigned char deck, kind;
		};
		std::vector<Change> changes;
		int seg = 0;
		float at = 0.0f;
		int lastSeg = (int)pts.size() - 2;
		int s0 = curSheet;
		unsigned char d0 = curDeck;
		for (size_t i = 0; i < chain.size(); ++i)
		{
			// Everything before this portal is on its right, or on its line (a string may run along the line a portal
			// lies on before it reaches the portal): the string crosses at the first point that is not on the right
			// and is within the portal's length.
			Portal p = nodes.portal(chain[i]);
			float plen = dist(p.left, p.right);
			if (plen <= 0.0f)
				plen = 1.0f;
			const float ON_IT = 1.0e-3f;
			for (;;)
			{
				Pt here;
				here.x = pts[seg].x + (pts[seg+1].x - pts[seg].x) * at;
				here.y = pts[seg].y + (pts[seg+1].y - pts[seg].y) * at;
				// d: how far to the left of the portal's line; s: how far along the portal from its left end.
				float dHere = cross(p.left, p.right, here) / plen, dEnd = cross(p.left, p.right, pts[seg+1]) / plen;
				float sHere = dot(p.left, p.right, here) / plen, sEnd = dot(p.left, p.right, pts[seg+1]) / plen;
				// The part [t0, t1] of what is left of this segment (0 = here, 1 = its end) that satisfies both.
				float t0 = 0.0f, t1 = 1.0f;
				if (dHere < -ON_IT)
				{
					if (dEnd < -ON_IT)
						t1 = -1.0f;
					else
						t0 = dHere / (dHere - dEnd);
				}
				if (sHere < -ON_IT)
				{
					if (sEnd < -ON_IT)
						t1 = -1.0f;
					else if (sHere / (sHere - sEnd) > t0)
						t0 = sHere / (sHere - sEnd);
				}
				else if (sHere > plen + ON_IT)
				{
					if (sEnd > plen + ON_IT)
						t1 = -1.0f;
					else if ((sHere - plen) / (sHere - sEnd) > t0)
						t0 = (sHere - plen) / (sHere - sEnd);
				}
				if (t0 <= t1)
				{
					at += (1.0f - at) * t0;
					if (at > 1.0f)
						at = 1.0f;
					break;
				}
				if (seg >= lastSeg)
				{
					at = 1.0f;
					break;
				}
				++seg;
				at = 0.0f;
			}
			int s1;
			const Tri& tr = triOf(atlas, nodes.tri(chain[i]), s1);
			unsigned char d1 = tr.label == NavBuild::SHEET_DECK ? 1 : 0;
			if (s1 != s0 || d1 != d0)
			{
				Change c;
				c.seg = seg;
				c.t = at;
				c.sheet = s1;
				c.deck = d1;
				c.kind = (unsigned char)(d1 != d0 ? 2 : 1);
				changes.push_back(c);
				s0 = s1;
				d0 = d1;
			}
		}

		// The string's own points with the changes between them. A change that falls on a corner is that corner.
		const float SAME = 1.0e-3f;
		size_t next = 0;
		for (int k = 0; k < (int)pts.size(); ++k)
		{
			for (; next < changes.size() && (float)changes[next].seg + changes[next].t < (float)k - SAME; ++next)
			{
				const Change& c = changes[next];
				Pt q;
				q.x = pts[c.seg].x + (pts[c.seg+1].x - pts[c.seg].x) * c.t;
				q.y = pts[c.seg].y + (pts[c.seg+1].y - pts[c.seg].y) * c.t;
				curSheet = c.sheet;
				curDeck = c.deck;
				if (dist(q, out.pts.back()) <= SAME)
				{
					// Two changes at one place (onto another sheet and onto its deck): one point, the later state.
					out.sheet.back() = curSheet;
					out.deck.back() = curDeck;
					if (out.inserted.back() != 0 && c.kind > out.inserted.back())
						out.inserted.back() = c.kind;
					continue;
				}
				out.pts.push_back(q);
				out.sheet.push_back(curSheet);
				out.deck.push_back(curDeck);
				out.inserted.push_back(c.kind);
			}
			for (; next < changes.size() && (float)changes[next].seg + changes[next].t <= (float)k + SAME; ++next)
			{
				curSheet = changes[next].sheet;
				curDeck = changes[next].deck;
			}
			out.pts.push_back(pts[k]);
			out.sheet.push_back(curSheet);
			out.deck.push_back(curDeck);
			out.inserted.push_back(0);
		}
	}

	/// Ways out of triangle g for a corridor that came in by search node 'node' (-1: it starts here): its open edges
	/// and its links to other sheets, except the one it came in by.
	void exitsOf( const Nodes& nodes, int g, int node, std::vector<int>& exits )
	{
		const Atlas& atlas = nodes.atlas;
		int sheet;
		const Tri& tr = triOf(atlas, g, sheet);
		const NavBuild::Mesh& mesh = *atlas.sheets[sheet].mesh;
		int base = atlas.triBase[sheet];
		int local = g - base;
		exits.clear();
		for (int e = 0; e < 3; ++e)
		{
			if (node >= 0 && !nodes.isLink(node) && e == node % 3)
				continue;
			int nb = tr.n[e];
			if (nb < 0 || !passable(SHEET_MASK, mesh.tris[nb].label))
				continue;
			exits.push_back((base + nb)*3 + backIndex(mesh, nb, local));
		}
		for (int k = atlas.linkStart[g]; k < atlas.linkStart[g+1]; ++k)
		{
			int j = atlas.linkOf[k];
			if (node >= 0 && nodes.isLink(node) && nodes.linkOf(node) == j)
				continue;
			exits.push_back(nodes.edgeNodes + j*2 + (atlas.links[j].triA == g ? 1 : 0));
		}
	}

	/// Some corridor from one triangle to another: the shortest by portal midpoints, one triangle at a time. Not the
	/// corridor of the shortest string, but one always exists when the two are connected, and it never passes
	/// through a triangle twice. The search below falls back on it (see there).
	bool anyCorridor( const Nodes& nodes, Pt start, int startTri, int goalTri, std::vector<int>& chain )
	{
		int total = totalTris(nodes.atlas);
		std::vector<float> d(total, BIG);
		std::vector<int> via(total, -1), prev(total, -1);
		std::vector<Pt> where(total);
		std::priority_queue<OpenNode, std::vector<OpenNode>, OpenOrder> open;
		std::vector<int> exits;
		unsigned seq = 0;
		d[startTri] = 0.0f;
		where[startTri] = start;
		OpenNode on;
		on.f = 0.0f;
		on.seq = seq++;
		on.node = startTri;
		on.reach = 0.0f;
		open.push(on);
		while (!open.empty())
		{
			OpenNode top = open.top();
			open.pop();
			int g = top.node;
			if (top.f != d[g])
				continue;
			if (g == goalTri)
				break;
			exitsOf(nodes, g, -1, exits);
			for (size_t x = 0; x < exits.size(); ++x)
			{
				int t2 = nodes.tri(exits[x]);
				Portal p = nodes.portal(exits[x]);
				Pt mid;
				mid.x = (p.left.x + p.right.x) * 0.5f;
				mid.y = (p.left.y + p.right.y) * 0.5f;
				float nd = d[g] + dist(where[g], mid);
				if (nd >= d[t2])
					continue;
				d[t2] = nd;
				via[t2] = exits[x];
				prev[t2] = g;
				where[t2] = mid;
				on.f = nd;
				on.seq = seq++;
				on.node = t2;
				open.push(on);
			}
		}
		chain.clear();
		if (d[goalTri] >= BIG)
			return false;
		for (int t = goalTri; t != startTri && t >= 0; t = prev[t])
			chain.push_back(via[t]);
		for (size_t a = 0, b = chain.size(); a + 1 < b; ++a, --b)
		{
			int tmp = chain[a]; chain[a] = chain[b-1]; chain[b-1] = tmp;
		}
		return true;
	}

	/// A* over the atlas, from a point in one triangle to a point in another.
	bool search( const Atlas& atlas, Pt start, Seat startSeat, Pt goal, Seat goalSeat, AtlasPath& out )
	{
		out.pts.clear();
		out.sheet.clear();
		out.deck.clear();
		out.inserted.clear();
		out.length = 0.0f;
		out.expanded = 0;
		out.corridors = 0;
		out.fallback = 0;
		Nodes nodes(atlas);
		int startTri = atlas.triBase[startSeat.sheet] + startSeat.tri;
		int goalTri = atlas.triBase[goalSeat.sheet] + goalSeat.tri;
		std::vector<int> chain;
		std::vector<Pt> pts;
		if (startTri == goalTri)
		{
			// In one triangle: the straight line.
			pts.push_back(start);
			pts.push_back(goal);
			describe(nodes, startTri, chain, pts, out);
			out.length = dist(start, goal);
			return true;
		}

		// Each search node has two numbers:
		//   reach   the length of the pulled string from the start, through the corridor that leads here, to the point
		//           of the entry portal nearest the goal. A real length: it decides which corridor a node keeps.
		//   bound   a lower bound on any way of getting to the entry portal. With the straight distance on to the goal
		//           it orders the open list, and it is what lets the search stop: when the smallest bound still open is
		//           no shorter than the best complete string, nothing unexplored can beat it.
		// Keeping one corridor per node is a heuristic (the corridor that reaches an edge soonest is not always the
		// start of the shortest way to the goal); against an exhaustive grid search it is longer than the grid's own
		// path in about 1 search in 3000, by at most a few percent.
		size_t numNodes = (size_t)nodes.count();
		std::vector<float> reach(numNodes, BIG);
		std::vector<float> bound(numNodes, BIG);
		std::vector<int> parent(numNodes, -1);
		std::priority_queue<OpenNode, std::vector<OpenNode>, OpenOrder> open;
		unsigned seq = 0;
		std::vector<Portal> portals, flat;
		std::vector<Pt> scratch;
		std::vector<Mirror> mirrors;
		bool folded = false;
		std::vector<int> exits, from;
		// A corridor never passes through a triangle twice (no shortest path does). Where ground meets a deck or
		// another sheet there are vertices with open ground all round them; a corridor could circle one, and the
		// funnel, which works in the plane, would pull the string of such a corridor straight through the vertex.
		std::vector<unsigned> inCorridor(totalTris(atlas), 0u);
		unsigned corridorStamp = 0;

		float best = BIG;
		std::vector<Pt> bestPts;
		std::vector<int> bestChain;
		int budget = (int)numNodes * 8 + 64;	// a corridor is replaced only by a shorter one, so this is never reached.

		// One pass of this loop per expansion; the first is the start triangle (node = -1).
		bool atStart = true;
		while ((atStart || !open.empty()) && --budget > 0)
		{
			int node, g;
			float boundHere = 0.0f, hHere = 0.0f;
			++corridorStamp;
			if (atStart)
			{
				atStart = false;
				node = -1;
				g = startTri;
				chain.clear();
				portals.clear();
				flat.clear();
				mirrors.clear();
				folded = false;
				inCorridor[g] = corridorStamp;
			}
			else
			{
				OpenNode top = open.top();
				open.pop();
				if (top.f >= best)
					break;
				node = top.node;
				if (top.reach != reach[node])
					continue;	// a shorter corridor to this node was found after this entry was queued.
				++out.expanded;
				g = nodes.tri(node);

				// The corridor that leads here. (Parents are replaced as shorter corridors turn up, so a chain read back
				// now is not always the one this node was queued with: drop it if it has come to pass through a triangle
				// twice.)
				chain.clear();
				int n;
				for (n = node; n >= 0 && chain.size() <= numNodes; n = parent[n])
					chain.push_back(n);
				for (size_t a = 0, b = chain.size(); a + 1 < b; ++a, --b)
				{
					int tmp = chain[a]; chain[a] = chain[b-1]; chain[b-1] = tmp;
				}
				bool simple = (n < 0);
				inCorridor[startTri] = corridorStamp;
				for (size_t q = 0; q < chain.size() && simple; ++q)
				{
					int tq = nodes.tri(chain[q]);
					if (inCorridor[tq] == corridorStamp)
						simple = false;
					inCorridor[tq] = corridorStamp;
				}
				if (!simple)
					continue;
				portals.clear();
				for (size_t q = 0; q < chain.size(); ++q)
					portals.push_back(nodes.portal(chain[q]));
				folded = unfold(portals, flat, mirrors);

				if (g == goalTri)
				{
					pull(portals, flat, mirrors, folded, 0, start, goal, pts, scratch, from);
					float len = lengthOf(pts);
					++out.corridors;
					if (len < best)
					{
						best = len;
						bestPts.swap(pts);
						bestChain = chain;
					}
					continue;
				}
				boundHere = bound[node];
				hHere = distToSegment(goal, portals.back().left, portals.back().right);
			}

			exitsOf(nodes, g, node, exits);

			for (size_t x = 0; x < exits.size(); ++x)
			{
				int node2 = exits[x];
				if (inCorridor[nodes.tri(node2)] == corridorStamp)
					continue;
				Portal p2 = nodes.portal(node2);
				Pt a = p2.right, b = p2.left;
				float reach2;
				if (node < 0)
				{
					reach2 = dist(start, closestOnSegment(goal, a, b));
				}
				else
				{
					pull(portals, flat, mirrors, folded, &p2, start, closestOnSegment(goal, a, b), pts, scratch, from);
					reach2 = lengthOf(pts);
				}
				if (reach2 >= reach[node2])
					continue;
				float hThere = distToSegment(goal, a, b);
				float bn = distToSegment(start, a, b);
				if (node >= 0)
				{
					float b2 = boundHere + (hHere - hThere);
					if (b2 > bn)
						bn = b2;
					if (bn < boundHere)
						bn = boundHere;
				}
				if (bn < bound[node2])
					bound[node2] = bn;
				reach[node2] = reach2;
				parent[node2] = node < 0 ? -1 : node;
				OpenNode on;
				on.f = bound[node2] + hThere;
				on.seq = seq++;
				on.node = node2;
				on.reach = reach2;
				open.push(on);
			}
		}

		if (best >= BIG)
		{
			// The search keeps one corridor per node and lets no corridor pass through a triangle twice. Together
			// those can, very rarely, close every way to a goal that is connected (the corridor a node kept cannot
			// go on without crossing itself, and the one that could was dropped). Then: any corridor, and its string.
			if (!anyCorridor(nodes, start, startTri, goalTri, bestChain))
				return false;
			portals.clear();
			for (size_t q = 0; q < bestChain.size(); ++q)
				portals.push_back(nodes.portal(bestChain[q]));
			folded = unfold(portals, flat, mirrors);
			pull(portals, flat, mirrors, folded, 0, start, goal, bestPts, scratch, from);
			best = lengthOf(bestPts);
			out.fallback = 1;
		}
		describe(nodes, startTri, bestChain, bestPts, out);
		out.length = best;
		return true;
	}

	/// An edge of a ground triangle with another sheet's ground (or this sheet's deck) across it, on the atlas lattice.
	struct OpenEdge
	{
		int tri, edge;				///< triangle numbered across all sheets
		int sheet;
		bool vertical;
		int line;							///< x of a vertical edge, y of a horizontal one
		int lo, hi;						///< its extent along the line
		bool rising;					///< runs from lo to hi (the triangle is then on the left looking that way)
	};
}

//-------------------------------------------------------------------------------------------------
void Atlas::clear()
{
	sheets.clear();
	links.clear();
	triBase.clear();
	linkStart.clear();
	linkOf.clear();
	component.clear();
}

//-------------------------------------------------------------------------------------------------
int Atlas::addSheet( const Mesh *mesh, int ox, int oy )
{
	Sheet s;
	s.mesh = mesh;
	s.ox = ox;
	s.oy = oy;
	triBase.push_back(totalTris(*this));
	sheets.push_back(s);
	return (int)sheets.size() - 1;
}

//-------------------------------------------------------------------------------------------------
void Atlas::connect()
{
	links.clear();
	int total = totalTris(*this);
	size_t k, j;

	// Where a sheet's ground stops because another sheet has that ground (NavBuild::assignSheets), its edge is a
	// stretch of a grid line, and the other sheet's ground has an edge on the same stretch from the other side.
	// The two meshes cut that line into edges each in its own way, so every pair of edges that overlap is joined
	// along what they share.
	std::vector<OpenEdge> edges;
	for (k = 0; k < sheets.size(); ++k)
	{
		const Sheet& sh = sheets[k];
		for (size_t t = 0; t < sh.mesh->tris.size(); ++t)
		{
			const Tri& tr = sh.mesh->tris[t];
			if (tr.label != NavBuild::SHEET_GROUND)
				continue;
			for (int e = 0; e < 3; ++e)
			{
				if (tr.n[e] < 0)
					continue;
				unsigned char across = sh.mesh->tris[tr.n[e]].label;
				if (across != NavBuild::SHEET_OTHER && across != NavBuild::SHEET_DECK)
					continue;
				int ax = sh.mesh->verts[tr.v[e]].x + sh.ox, ay = sh.mesh->verts[tr.v[e]].y + sh.oy;
				int bx = sh.mesh->verts[tr.v[(e+1)%3]].x + sh.ox, by = sh.mesh->verts[tr.v[(e+1)%3]].y + sh.oy;
				if (ax != bx && ay != by)
					continue;	// these boundaries are kept on the grid's lines; a slanted edge is not one of them
				OpenEdge oe;
				oe.tri = triBase[k] + (int)t;
				oe.edge = e;
				oe.sheet = (int)k;
				oe.vertical = (ax == bx);
				oe.line = oe.vertical ? ax : ay;
				int from = oe.vertical ? ay : ax, to = oe.vertical ? by : bx;
				oe.rising = to > from;
				oe.lo = oe.rising ? from : to;
				oe.hi = oe.rising ? to : from;
				edges.push_back(oe);
			}
		}
	}
	for (k = 0; k < edges.size(); ++k)
	{
		const OpenEdge& ea = edges[k];
		for (j = k + 1; j < edges.size(); ++j)
		{
			const OpenEdge& eb = edges[j];
			// Opposite directions on one line: the two triangles are on opposite sides of it.
			if (eb.sheet == ea.sheet || eb.vertical != ea.vertical || eb.line != ea.line || eb.rising == ea.rising)
				continue;
			int lo = ea.lo > eb.lo ? ea.lo : eb.lo, hi = ea.hi < eb.hi ? ea.hi : eb.hi;
			if ((float)(hi - lo) <= MIN_PORTAL)
				continue;
			Link l;
			l.triA = ea.tri;
			l.edgeA = ea.edge;
			l.triB = eb.tri;
			l.edgeB = eb.edge;
			int sa = ea.rising ? lo : hi, sb = ea.rising ? hi : lo;
			l.a.x = (float)(ea.vertical ? ea.line : sa);
			l.a.y = (float)(ea.vertical ? sa : ea.line);
			l.b.x = (float)(ea.vertical ? ea.line : sb);
			l.b.y = (float)(ea.vertical ? sb : ea.line);
			links.push_back(l);
		}
	}

	// Each triangle's links.
	linkStart.assign(total + 1, 0);
	for (k = 0; k < links.size(); ++k)
	{
		++linkStart[links[k].triA + 1];
		++linkStart[links[k].triB + 1];
	}
	for (int g = 0; g < total; ++g)
		linkStart[g+1] += linkStart[g];
	linkOf.assign(links.size() * 2, 0);
	std::vector<int> fill(linkStart.begin(), linkStart.end() - 1);
	for (k = 0; k < links.size(); ++k)
	{
		linkOf[fill[links[k].triA]++] = (int)k;
		linkOf[fill[links[k].triB]++] = (int)k;
	}

	// Connected pieces of passable ground, over edges and links.
	component.assign(total, -1);
	std::vector<int> stack;
	int pieces = 0;
	for (int g0 = 0; g0 < total; ++g0)
	{
		int sheet;
		if (component[g0] >= 0 || !passable(SHEET_MASK, triOf(*this, g0, sheet).label))
			continue;
		component[g0] = pieces;
		stack.push_back(g0);
		while (!stack.empty())
		{
			int g = stack.back();
			stack.pop_back();
			const Tri& tr = triOf(*this, g, sheet);
			const Mesh& mesh = *sheets[sheet].mesh;
			for (int e = 0; e < 3; ++e)
			{
				int nb = tr.n[e];
				if (nb >= 0 && passable(SHEET_MASK, mesh.tris[nb].label) && component[triBase[sheet] + nb] < 0)
				{
					component[triBase[sheet] + nb] = pieces;
					stack.push_back(triBase[sheet] + nb);
				}
			}
			for (int q = linkStart[g]; q < linkStart[g+1]; ++q)
			{
				const Link& l = links[linkOf[q]];
				int other = l.triA == g ? l.triB : l.triA;
				if (component[other] < 0)
				{
					component[other] = pieces;
					stack.push_back(other);
				}
			}
		}
		++pieces;
	}
}

//-------------------------------------------------------------------------------------------------
bool findPath( const Mesh& mesh, Pt start, Pt goal, unsigned mask, Path& out )
{
	out.pts.clear();
	out.length = 0.0f;
	out.expanded = 0;
	out.corridors = 0;
	int startTri = locate(mesh, start, mask);
	int goalTri = locate(mesh, goal, mask);
	if (startTri < 0 || goalTri < 0 || !passable(mask, mesh.tris[startTri].label) || !passable(mask, mesh.tris[goalTri].label))
		return false;
	// One sheet and no links. The search knows sheet labels only: a mesh with other labels is searched through a
	// copy that has them.
	Atlas atlas;
	Mesh relabelled;
	const Mesh *use = &mesh;
	bool plain = (mask == GROUND_MASK);
	for (size_t q = 0; q < mesh.tris.size() && plain; ++q)
		plain = mesh.tris[q].label <= NavBuild::SHEET_WALL;
	if (!plain)
	{
		relabelled = mesh;
		for (size_t t = 0; t < relabelled.tris.size(); ++t)
			relabelled.tris[t].label = passable(mask, mesh.tris[t].label) ? (unsigned char)NavBuild::SHEET_GROUND : (unsigned char)NavBuild::SHEET_WALL;
		use = &relabelled;
	}
	atlas.addSheet(use, 0, 0);
	atlas.linkStart.assign(use->tris.size() + 1, 0);
	Seat from, to;
	from.sheet = 0; from.tri = startTri;
	to.sheet = 0; to.tri = goalTri;
	AtlasPath found;
	bool ok = search(atlas, start, from, goal, to, found);
	out.expanded = found.expanded;
	out.corridors = found.corridors;
	if (!ok)
		return false;
	out.pts.swap(found.pts);
	out.length = found.length;
	return true;
}

//-------------------------------------------------------------------------------------------------
namespace
{
	/// The spot nearest p that a unit may stand on: on the deck of sheet 'deck' if it may be on that bridge, on any
	/// sheet's ground if it may be on the ground. With wantComponent >= 0 only spots of that connected piece count.
	bool seatOf( const Atlas& atlas, Pt p, int deck, bool ground, float reach, int wantComponent, Pt& at, Seat& seat )
	{
		bool found = false;
		float bestDist = 0.0f;
		bool onDeck = deck > 0 && deck < (int)atlas.sheets.size();
		// The bridge's own sheet first, so that at a bridge's end, where the unit may be on either, the bridge wins.
		for (int pass = 0; pass < 2; ++pass)
		{
			for (int s = 0; s < (int)atlas.sheets.size(); ++s)
			{
				unsigned mask;
				if (pass == 0)
				{
					if (!onDeck || s != deck)
						continue;
					mask = SHEET_MASK;
				}
				else
				{
					if ((onDeck && !ground) || (onDeck && s == deck))
						continue;
					mask = GROUND_MASK;
				}
				const Sheet& sh = atlas.sheets[s];
				Pt local;
				local.x = p.x - (float)sh.ox;
				local.y = p.y - (float)sh.oy;
				if (local.x < -reach || local.y < -reach || local.x > (float)sh.mesh->width + reach || local.y > (float)sh.mesh->height + reach)
					continue;	// nothing of this sheet is near
				Pt q;
				int t;
				if (!nearestPassable(*sh.mesh, local, mask, reach, wantComponent >= 0 ? &atlas.component[atlas.triBase[s]] : 0,
														 wantComponent, q, t))
					continue;
				float d = dist(local, q);
				if (!found || d < bestDist)
				{
					found = true;
					bestDist = d;
					at.x = q.x + (float)sh.ox;
					at.y = q.y + (float)sh.oy;
					seat.sheet = s;
					seat.tri = t;
				}
			}
		}
		return found;
	}
}

//-------------------------------------------------------------------------------------------------
int findPath( const Atlas& atlas, Pt start, int startDeck, bool startGround, Pt goal, int goalDeck, bool goalGround,
							float reach, AtlasPath& out )
{
	out.pts.clear();
	out.sheet.clear();
	out.deck.clear();
	out.inserted.clear();
	out.length = 0.0f;
	out.expanded = 0;
	out.corridors = 0;
	out.fallback = 0;
	if (atlas.sheets.empty())
		return PATH_NO_START;
	Seat from, to;
	Pt s, g;
	if (!seatOf(atlas, start, startDeck, startGround, reach, -1, s, from))
		return PATH_NO_START;
	// The goal: the nearest spot that is connected to where the unit is.
	if (!seatOf(atlas, goal, goalDeck, goalGround, reach, atlas.component[atlas.triBase[from.sheet] + from.tri], g, to))
		return PATH_NO_GOAL;
	return search(atlas, s, from, g, to, out) ? PATH_FOUND : PATH_NONE;
}

}	// namespace NavQuery
