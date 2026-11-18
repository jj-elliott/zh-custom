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
	/// Is edge i of triangle t a wall for this unit?
	inline bool blocked( const Mesh& mesh, int t, int i, unsigned mask )
	{
		int nb = mesh.tris[t].n[i];
		return nb < 0 || !passable(mask, mesh.tris[nb].label);
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

	/// The edge crossed when entering triangle (node/3) through its edge (node%3), as the traveller sees it.
	Portal portalOf( const Mesh& mesh, int node )
	{
		const Tri& ct = mesh.tris[node / 3];
		int ce = node % 3;
		// Entering across edge ce (v[ce] -> v[ce+1], inside on its left): v[ce] is on our left.
		Portal p;
		p.left = pointOf(mesh, ct.v[ce]);
		p.right = pointOf(mesh, ct.v[(ce+1)%3]);
		return p;
	}

	/// The funnel ("simple stupid funnel algorithm"): the shortest line through a row of portals.
	/// Mesh vertices are lattice points, so collinear cases give cross products of exactly zero; a zero counts as
	/// "still inside the funnel" (a start that lies on a portal's line must not turn that portal's end into a corner).
	void funnel( const std::vector<Portal>& portals, const Pt& start, const Pt& goal, std::vector<Pt>& out )
	{
		out.clear();
		out.push_back(start);
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
					apex = right;
					i = rightIndex;
					left = apex; right = apex;
					leftIndex = rightIndex;
					continue;
				}
			}
		}
		if (out.back().x != goal.x || out.back().y != goal.y)
			out.push_back(goal);
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
	if (startTri == goalTri)
	{
		out.pts.push_back(start);
		out.pts.push_back(goal);
		out.length = dist(start, goal);
		return true;
	}

	// A search node is "triangle t, entered through its edge e": t*3 + e. Each has two numbers:
	//   reach   the length of the pulled string from the start, through the corridor that leads here, to the point
	//           of the entry edge nearest the goal. A real length: it decides which corridor a node keeps.
	//   bound   a lower bound on any way of getting to the entry edge. With the straight distance on to the goal it
	//           orders the open list, and it is what lets the search stop: when the smallest bound still open is no
	//           shorter than the best complete string, nothing unexplored can beat it.
	// Keeping one corridor per node is a heuristic (the corridor that reaches an edge soonest is not always the
	// start of the shortest way to the goal); against an exhaustive grid search it is longer than the grid's own
	// path in about 1 search in 3000, by at most a few percent.
	size_t numNodes = mesh.tris.size() * 3;
	std::vector<float> reach(numNodes, BIG);
	std::vector<float> bound(numNodes, BIG);
	std::vector<int> parent(numNodes, -1);
	std::priority_queue<OpenNode, std::vector<OpenNode>, OpenOrder> open;
	unsigned seq = 0;
	std::vector<Portal> portals;
	std::vector<int> chain;
	std::vector<Pt> pts;

	{
		const Tri& tr = mesh.tris[startTri];
		for (int i = 0; i < 3; ++i)
		{
			if (blocked(mesh, startTri, i, mask))
				continue;
			Pt a = pointOf(mesh, tr.v[i]), b = pointOf(mesh, tr.v[(i+1)%3]);
			int nb = tr.n[i];
			int node = nb*3 + backIndex(mesh, nb, startTri);
			reach[node] = dist(start, closestOnSegment(goal, a, b));
			bound[node] = distToSegment(start, a, b);
			OpenNode on;
			on.f = bound[node] + distToSegment(goal, a, b);
			on.seq = seq++;
			on.node = node;
			on.reach = reach[node];
			open.push(on);
		}
	}

	float best = BIG;
	std::vector<Pt> bestPts;
	int budget = (int)numNodes * 8 + 64;	// a corridor is replaced only by a shorter one, so this is never reached.
	while (!open.empty() && --budget > 0)
	{
		OpenNode top = open.top();
		open.pop();
		if (top.f >= best)
			break;
		int node = top.node;
		if (top.reach != reach[node])
			continue;	// a shorter corridor to this node was found after this entry was queued.
		++out.expanded;
		int t = node / 3, e = node % 3;
		const Tri& tr = mesh.tris[t];

		// The portals of the corridor that leads here.
		chain.clear();
		for (int n = node; n >= 0 && chain.size() <= numNodes; n = parent[n])
			chain.push_back(n);
		portals.clear();
		for (int k = (int)chain.size() - 1; k >= 0; --k)
			portals.push_back(portalOf(mesh, chain[k]));

		if (t == goalTri)
		{
			funnel(portals, start, goal, pts);
			float len = lengthOf(pts);
			++out.corridors;
			if (len < best)
			{
				best = len;
				bestPts.swap(pts);
			}
			continue;
		}

		Pt ea = pointOf(mesh, tr.v[e]), eb = pointOf(mesh, tr.v[(e+1)%3]);
		float hHere = distToSegment(goal, ea, eb);
		for (int k = 1; k <= 2; ++k)
		{
			int eo = (e + k) % 3;
			if (blocked(mesh, t, eo, mask))
				continue;
			Pt a = pointOf(mesh, tr.v[eo]), b = pointOf(mesh, tr.v[(eo+1)%3]);
			int nb = tr.n[eo];
			int node2 = nb*3 + backIndex(mesh, nb, t);
			funnel(portals, start, closestOnSegment(goal, a, b), pts);
			float reach2 = lengthOf(pts);
			if (reach2 >= reach[node2])
				continue;
			float hThere = distToSegment(goal, a, b);
			float b1 = distToSegment(start, a, b);
			float b2 = bound[node] + (hHere - hThere);
			float bn = b1 > b2 ? b1 : b2;
			if (bn < bound[node])
				bn = bound[node];
			if (bn < bound[node2])
				bound[node2] = bn;
			reach[node2] = reach2;
			parent[node2] = node;
			OpenNode on;
			on.f = bound[node2] + hThere;
			on.seq = seq++;
			on.node = node2;
			on.reach = reach2;
			open.push(on);
		}
	}

	if (best >= BIG)
		return false;
	out.pts.swap(bestPts);
	out.length = best;
	return true;
}

}	// namespace NavQuery
