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

// zh-pipeline @feature 2026-11-17 Navigation mesh construction, see NavMeshBuild.h.

#ifndef NAVMESH_STANDALONE
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine
#endif

#include "GameLogic/NavMeshBuild.h"

#include <stdio.h>
#include <string.h>

namespace NavBuild
{

//-------------------------------------------------------------------------------------------------
I64 orient( const Vert& a, const Vert& b, const Vert& c )
{
	return (I64)(b.x - a.x) * (I64)(c.y - a.y) - (I64)(b.y - a.y) * (I64)(c.x - a.x);
}

//-------------------------------------------------------------------------------------------------
I64 inCircle( const Vert& a, const Vert& b, const Vert& c, const Vert& d )
{
	I64 adx = a.x - d.x, ady = a.y - d.y;
	I64 bdx = b.x - d.x, bdy = b.y - d.y;
	I64 cdx = c.x - d.x, cdy = c.y - d.y;
	I64 ad2 = adx*adx + ady*ady;
	I64 bd2 = bdx*bdx + bdy*bdy;
	I64 cd2 = cdx*cdx + cdy*cdy;
	return adx * (bdy*cd2 - cdy*bd2) - ady * (bdx*cd2 - cdx*bd2) + ad2 * (bdx*cdy - cdx*bdy);
}

namespace
{
	inline I64 dotFrom( const Vert& a, const Vert& b, const Vert& c )
	{
		return (I64)(b.x - a.x) * (I64)(c.x - a.x) + (I64)(b.y - a.y) * (I64)(c.y - a.y);
	}
	inline int indexOf( const Tri& t, int v )
	{
		return t.v[0] == v ? 0 : (t.v[1] == v ? 1 : (t.v[2] == v ? 2 : -1));
	}
	inline bool oppositeSides( I64 a, I64 b )
	{
		return (a > 0 && b < 0) || (a < 0 && b > 0);
	}
}

//-------------------------------------------------------------------------------------------------
void CDT::init( int w, int h )
{
	verts.clear();
	tris.clear();
	m_vtri.clear();
	Vert v;
	v.x = 0; v.y = 0; verts.push_back(v);
	v.x = w; v.y = 0; verts.push_back(v);
	v.x = w; v.y = h; verts.push_back(v);
	v.x = 0; v.y = h; verts.push_back(v);
	m_vtri.resize(4, 0);
	Tri t;
	memset(&t, 0, sizeof(t));
	t.label = LABEL_NONE;
	t.v[0] = 0; t.v[1] = 1; t.v[2] = 2;
	t.n[0] = -1; t.n[1] = -1; t.n[2] = 1;
	tris.push_back(t);
	t.v[0] = 0; t.v[1] = 2; t.v[2] = 3;
	t.n[0] = 0; t.n[1] = -1; t.n[2] = -1;
	tris.push_back(t);
	setTri(0);
	setTri(1);
	m_last = 0;
}

//-------------------------------------------------------------------------------------------------
void CDT::setTri( int t )
{
	m_vtri[tris[t].v[0]] = t;
	m_vtri[tris[t].v[1]] = t;
	m_vtri[tris[t].v[2]] = t;
}

//-------------------------------------------------------------------------------------------------
int CDT::backIndex( int nb, int t ) const
{
	const Tri& n = tris[nb];
	return n.n[0] == t ? 0 : (n.n[1] == t ? 1 : 2);
}

//-------------------------------------------------------------------------------------------------
void CDT::rotateTo( int t, int i )
{
	if (i == 0)
		return;
	Tri old = tris[t];
	Tri& tr = tris[t];
	for (int k = 0; k < 3; ++k)
	{
		tr.v[k] = old.v[(k + i) % 3];
		tr.n[k] = old.n[(k + i) % 3];
		tr.c[k] = old.c[(k + i) % 3];
	}
}

//-------------------------------------------------------------------------------------------------
/** Which triangle holds the point. where: -1 inside, 0..2 on that edge, 3..5 it is that vertex. */
int CDT::locate( int x, int y, int& where )
{
	Vert p;
	p.x = x; p.y = y;
	int t = m_last;
	int guard = (int)tris.size() * 2 + 16;
	for (;;)
	{
		const Tri& tr = tris[t];
		int moved = -1;
		I64 o[3];
		for (int i = 0; i < 3; ++i)
		{
			o[i] = orient(verts[tr.v[i]], verts[tr.v[(i+1)%3]], p);
			if (o[i] < 0 && tr.n[i] >= 0 && moved < 0)
				moved = tr.n[i];
		}
		if (moved >= 0 && --guard > 0)
		{
			t = moved;
			continue;
		}
		if (moved >= 0)
		{
			// The walk went round in circles (possible once constraints are in): look at every triangle instead.
			for (t = 0; t < (int)tris.size(); ++t)
			{
				const Tri& s = tris[t];
				if (orient(verts[s.v[0]], verts[s.v[1]], p) >= 0 && orient(verts[s.v[1]], verts[s.v[2]], p) >= 0
						&& orient(verts[s.v[2]], verts[s.v[0]], p) >= 0)
					break;
			}
			if (t == (int)tris.size())
				t = 0;	// outside the rectangle: the caller's fault.
			guard = 0x7fffffff;
			m_last = t;
			continue;
		}
		where = -1;
		for (int i = 0; i < 3; ++i)
		{
			if (verts[tr.v[i]].x == x && verts[tr.v[i]].y == y)
			{
				where = 3 + i;
				return t;
			}
		}
		for (int i = 0; i < 3; ++i)
		{
			if (o[i] == 0)
				where = i;
		}
		return t;
	}
}

//-------------------------------------------------------------------------------------------------
/** Replace the edge i of t with the other diagonal of the quad it and its neighbour form. Afterwards the old apex
		of t is v[2] of both triangles and the edges that were not touched by it are edge 0 of each. */
void CDT::flip( int t, int i )
{
	rotateTo(t, i);
	int nb = tris[t].n[0];
	rotateTo(nb, backIndex(nb, t));
	Tri T = tris[t];	// (a, b, c)
	Tri N = tris[nb];	// (b, a, d)
	int a = T.v[0], b = T.v[1], c = T.v[2], d = N.v[2];
	if (N.n[1] >= 0)
		tris[N.n[1]].n[backIndex(N.n[1], nb)] = t;
	if (T.n[1] >= 0)
		tris[T.n[1]].n[backIndex(T.n[1], t)] = nb;
	Tri& A = tris[t];
	A.v[0] = a; A.v[1] = d; A.v[2] = c;
	A.n[0] = N.n[1]; A.n[1] = nb; A.n[2] = T.n[2];
	A.c[0] = N.c[1]; A.c[1] = 0; A.c[2] = T.c[2];
	Tri& B = tris[nb];
	B.v[0] = d; B.v[1] = b; B.v[2] = c;
	B.n[0] = N.n[2]; B.n[1] = T.n[1]; B.n[2] = t;
	B.c[0] = N.c[2]; B.c[1] = T.c[1]; B.c[2] = 0;
	setTri(t);
	setTri(nb);
}

//-------------------------------------------------------------------------------------------------
void CDT::legalize( std::vector<int>& stack )
{
	while (!stack.empty())
	{
		int e = stack.back();
		stack.pop_back();
		int t = e / 3, i = e % 3;
		const Tri& tr = tris[t];
		int nb = tr.n[i];
		if (nb < 0 || tr.c[i])
			continue;
		int j = backIndex(nb, t);
		int d = tris[nb].v[(j+2)%3];
		if (inCircle(verts[tr.v[0]], verts[tr.v[1]], verts[tr.v[2]], verts[d]) > 0)
		{
			flip(t, i);
			stack.push_back(t*3);
			stack.push_back(nb*3);
		}
	}
}

//-------------------------------------------------------------------------------------------------
int CDT::addVertex( int x, int y )
{
	int where;
	int t = locate(x, y, where);
	if (where >= 3)
		return tris[t].v[where - 3];

	int p = (int)verts.size();
	Vert v;
	v.x = x; v.y = y;
	verts.push_back(v);
	m_vtri.push_back(-1);
	std::vector<int> stack;

	if (where < 0)
	{
		Tri T = tris[t];	// (a, b, c)
		int t1 = (int)tris.size();
		int t2 = t1 + 1;
		if (T.n[1] >= 0)
			tris[T.n[1]].n[backIndex(T.n[1], t)] = t1;
		if (T.n[2] >= 0)
			tris[T.n[2]].n[backIndex(T.n[2], t)] = t2;
		Tri A = T, B = T, C = T;
		A.v[0] = T.v[0]; A.v[1] = T.v[1]; A.v[2] = p;
		A.n[0] = T.n[0]; A.n[1] = t1; A.n[2] = t2;
		A.c[0] = T.c[0]; A.c[1] = 0; A.c[2] = 0;
		B.v[0] = T.v[1]; B.v[1] = T.v[2]; B.v[2] = p;
		B.n[0] = T.n[1]; B.n[1] = t2; B.n[2] = t;
		B.c[0] = T.c[1]; B.c[1] = 0; B.c[2] = 0;
		C.v[0] = T.v[2]; C.v[1] = T.v[0]; C.v[2] = p;
		C.n[0] = T.n[2]; C.n[1] = t; C.n[2] = t1;
		C.c[0] = T.c[2]; C.c[1] = 0; C.c[2] = 0;
		tris[t] = A;
		tris.push_back(B);
		tris.push_back(C);
		setTri(t); setTri(t1); setTri(t2);
		stack.push_back(t*3); stack.push_back(t1*3); stack.push_back(t2*3);
	}
	else
	{
		rotateTo(t, where);
		int u = tris[t].n[0];
		if (u >= 0)
			rotateTo(u, backIndex(u, t));
		Tri T = tris[t];	// (a, b, c), the point is on a->b
		unsigned char k = T.c[0];
		int tn = (int)tris.size();
		int un = (u >= 0) ? tn + 1 : -1;
		if (T.n[1] >= 0)
			tris[T.n[1]].n[backIndex(T.n[1], t)] = tn;
		Tri A = T, B = T;
		A.v[0] = T.v[0]; A.v[1] = p; A.v[2] = T.v[2];
		A.n[0] = un; A.n[1] = tn; A.n[2] = T.n[2];
		A.c[0] = k; A.c[1] = 0; A.c[2] = T.c[2];
		B.v[0] = p; B.v[1] = T.v[1]; B.v[2] = T.v[2];
		B.n[0] = u; B.n[1] = T.n[1]; B.n[2] = t;
		B.c[0] = k; B.c[1] = T.c[1]; B.c[2] = 0;
		Tri C, D;
		if (u >= 0)
		{
			Tri U = tris[u];	// (b, a, d)
			if (U.n[1] >= 0)
				tris[U.n[1]].n[backIndex(U.n[1], u)] = un;
			C = U; D = U;
			C.v[0] = U.v[0]; C.v[1] = p; C.v[2] = U.v[2];
			C.n[0] = tn; C.n[1] = un; C.n[2] = U.n[2];
			C.c[0] = k; C.c[1] = 0; C.c[2] = U.c[2];
			D.v[0] = p; D.v[1] = U.v[1]; D.v[2] = U.v[2];
			D.n[0] = t; D.n[1] = U.n[1]; D.n[2] = u;
			D.c[0] = k; D.c[1] = U.c[1]; D.c[2] = 0;
		}
		tris[t] = A;
		tris.push_back(B);
		setTri(t); setTri(tn);
		stack.push_back(t*3 + 2); stack.push_back(tn*3 + 1);
		if (u >= 0)
		{
			tris[u] = C;
			tris.push_back(D);
			setTri(u); setTri(un);
			stack.push_back(u*3 + 2); stack.push_back(un*3 + 1);
		}
	}
	legalize(stack);
	m_last = m_vtri[p];
	return p;
}

//-------------------------------------------------------------------------------------------------
int CDT::findEdge( int u, int v ) const
{
	int t0 = m_vtri[u];
	int t = t0;
	do
	{
		const Tri& tr = tris[t];
		int k = indexOf(tr, u);
		if (tr.v[(k+1)%3] == v)
			return t*3 + k;
		t = tr.n[(k+2)%3];
	} while (t >= 0 && t != t0);
	if (t < 0)
	{
		t = t0;
		for (;;)
		{
			const Tri& tr = tris[t];
			t = tr.n[indexOf(tr, u)];
			if (t < 0)
				break;
			const Tri& nx = tris[t];
			int k = indexOf(nx, u);
			if (nx.v[(k+1)%3] == v)
				return t*3 + k;
		}
	}
	return -1;
}

//-------------------------------------------------------------------------------------------------
void CDT::markConstrained( int u, int v )
{
	int e = findEdge(u, v);
	if (e >= 0)
		tris[e/3].c[e%3] = 1;
	e = findEdge(v, u);
	if (e >= 0)
		tris[e/3].c[e%3] = 1;
}

//-------------------------------------------------------------------------------------------------
/** Make the segment a-b a chain of constrained edges. Where it passes exactly through other vertices it is cut
		there. Method: Sloan's edge flipping: every edge the segment crosses is flipped until none crosses it,
		then the edges this created are put back to Delaunay where the constraint allows. */
void CDT::addConstraint( int a, int b )
{
	std::vector<int> starTris;
	std::vector<int> crossed;		// pairs of vertices
	std::vector<int> created;		// pairs of vertices
	int outerGuard = (int)verts.size() + 8;
	while (a != b && --outerGuard > 0)
	{
		const Vert A = verts[a];
		const Vert B = verts[b];

		// The triangles round a, in a fixed order.
		starTris.clear();
		{
			int t0 = m_vtri[a];
			int t = t0;
			do
			{
				starTris.push_back(t);
				const Tri& tr = tris[t];
				t = tr.n[(indexOf(tr, a) + 2) % 3];
			} while (t >= 0 && t != t0);
			if (t < 0)
			{
				t = t0;
				for (;;)
				{
					const Tri& tr = tris[t];
					t = tr.n[indexOf(tr, a)];
					if (t < 0)
						break;
					starTris.push_back(t);
				}
			}
		}

		int next = -1;
		int startTri = -1, startEdge = -1, cr = -1, cl = -1;
		for (size_t s = 0; s < starTris.size() && next < 0 && startTri < 0; ++s)
		{
			const Tri& tr = tris[starTris[s]];
			int k = indexOf(tr, a);
			int r = tr.v[(k+1)%3], l = tr.v[(k+2)%3];
			I64 orr = orient(A, B, verts[r]);
			I64 orl = orient(A, B, verts[l]);
			if (orr == 0 && dotFrom(A, B, verts[r]) > 0)
				next = r;
			else if (orl == 0 && dotFrom(A, B, verts[l]) > 0)
				next = l;
			else if (orr < 0 && orl > 0)
			{
				startTri = starTris[s];
				startEdge = (k+1)%3;
				cr = r; cl = l;
			}
		}
		if (next >= 0)
		{
			markConstrained(a, next);
			a = next;
			continue;
		}
		if (startTri < 0)
			return;	// cannot happen in a valid triangulation.

		// March along the segment collecting the edges it crosses, up to b or to the first vertex lying on it.
		crossed.clear();
		created.clear();
		int end = -1;
		{
			int ct = startTri, ce = startEdge;
			int guard = (int)tris.size() + 8;
			while (--guard > 0)
			{
				crossed.push_back(cr);
				crossed.push_back(cl);
				int nb = tris[ct].n[ce];
				if (nb < 0)
					return;	// cannot happen: the segment is inside the rectangle.
				int j = backIndex(nb, ct);
				int w = tris[nb].v[(j+2)%3];
				I64 o = orient(A, B, verts[w]);
				if (w == b || o == 0)
				{
					end = w;
					break;
				}
				if (o > 0)
				{
					ce = (j+1)%3;
					cl = w;
				}
				else
				{
					ce = (j+2)%3;
					cr = w;
				}
				ct = nb;
			}
			if (end < 0)
				return;
		}
		const Vert E = verts[end];

		size_t head = 0;
		size_t guard = crossed.size() * crossed.size() + 4096;
		while (head < crossed.size() && --guard > 0)
		{
			int u = crossed[head], v = crossed[head+1];
			head += 2;
			int e = findEdge(u, v);
			if (e < 0)
				continue;
			int t = e / 3;
			rotateTo(t, e % 3);
			int nb = tris[t].n[0];
			if (nb < 0 || tris[t].c[0])
				continue;	// a constraint crossing a constraint: the caller broke the contract. Leave it.
			int c = tris[t].v[2];
			int d = tris[nb].v[(backIndex(nb, t) + 2) % 3];
			// Only a strictly convex quad can have its diagonal flipped.
			if (!oppositeSides(orient(verts[c], verts[d], verts[u]), orient(verts[c], verts[d], verts[v])))
			{
				crossed.push_back(u);
				crossed.push_back(v);
				continue;
			}
			flip(t, 0);
			if (c != a && c != end && d != a && d != end
					&& oppositeSides(orient(A, E, verts[c]), orient(A, E, verts[d])))
			{
				crossed.push_back(c);
				crossed.push_back(d);
			}
			else
			{
				created.push_back(c);
				created.push_back(d);
			}
		}

		markConstrained(a, end);

		// Put the created edges back to Delaunay (the constraint itself stays).
		bool changed = true;
		int passes = 64;
		while (changed && --passes > 0)
		{
			changed = false;
			for (size_t n = 0; n + 1 < created.size(); n += 2)
			{
				int u = created[n], v = created[n+1];
				int e = findEdge(u, v);
				if (e < 0)
					continue;
				int t = e / 3, i = e % 3;
				const Tri& tr = tris[t];
				int nb = tr.n[i];
				if (tr.c[i] || nb < 0)
					continue;
				int c = tr.v[(i+2)%3];
				int d = tris[nb].v[(backIndex(nb, t) + 2) % 3];
				if (inCircle(verts[tr.v[0]], verts[tr.v[1]], verts[tr.v[2]], verts[d]) > 0)
				{
					flip(t, i);
					created[n] = c;
					created[n+1] = d;
					changed = true;
				}
			}
		}
		a = end;
	}
}

//=================================================================================================
// Boundaries of the label grid
//=================================================================================================
namespace
{
	enum { OUTSIDE = 254 };

	struct Chain
	{
		std::vector<int> pts;		///< lattice vertex ids, every unit step
		std::vector<int> kept;	///< indices into pts that survive simplification (always first and last)
		unsigned char left, right;
		bool exact;
	};

	struct Grid
	{
		const unsigned char *labels;
		int w, h;
		unsigned char cell( int x, int y ) const
		{
			if (x < 0 || y < 0 || x >= w || y >= h)
				return OUTSIDE;
			return labels[y*w + x];
		}
		int vid( int x, int y ) const { return y*(w+1) + x; }
		// Unit edges leaving lattice point (x, y): 0 east, 1 north, 2 west, 3 south.
		bool isBoundary( int x, int y, int dir ) const
		{
			switch (dir)
			{
				case 0: return x < w && cell(x, y) != cell(x, y-1);
				case 1: return y < h && cell(x-1, y) != cell(x, y);
				case 2: return x > 0 && cell(x-1, y) != cell(x-1, y-1);
				default: return y > 0 && cell(x-1, y-1) != cell(x, y-1);
			}
		}
		// A fixed id per unit edge: horizontal ones first.
		int edgeId( int x, int y, int dir ) const
		{
			switch (dir)
			{
				case 0: return y*w + x;
				case 2: return y*w + x - 1;
				case 1: return (h+1)*w + y*(w+1) + x;
				default: return (h+1)*w + (y-1)*(w+1) + x;
			}
		}
		unsigned char leftOf( int x, int y, int dir ) const
		{
			switch (dir)
			{
				case 0: return cell(x, y);
				case 1: return cell(x-1, y);
				case 2: return cell(x-1, y-1);
				default: return cell(x, y-1);
			}
		}
		unsigned char rightOf( int x, int y, int dir ) const
		{
			switch (dir)
			{
				case 0: return cell(x, y-1);
				case 1: return cell(x, y);
				case 2: return cell(x-1, y);
				default: return cell(x-1, y-1);
			}
		}
	};

	const int DX[4] = { 1, 0, -1, 0 };
	const int DY[4] = { 0, 1, 0, -1 };

	inline Vert latticePoint( const Grid& g, int id )
	{
		Vert v;
		v.x = id % (g.w + 1);
		v.y = id / (g.w + 1);
		return v;
	}

	/// Follow the boundary from (x, y) in direction dir until the next node (or back to the start, for a loop).
	void follow( const Grid& g, const std::vector<unsigned char>& isNode, std::vector<unsigned char>& used,
								int x, int y, int dir, Chain& chain )
	{
		chain.left = g.leftOf(x, y, dir);
		chain.right = g.rightOf(x, y, dir);
		chain.exact = false;
		chain.pts.push_back(g.vid(x, y));
		int startId = g.vid(x, y);
		for (;;)
		{
			used[g.edgeId(x, y, dir)] = 1;
			x += DX[dir];
			y += DY[dir];
			int id = g.vid(x, y);
			chain.pts.push_back(id);
			if (isNode[id] || id == startId)
				break;
			// A point that is not a node has exactly two boundary edges: leave by the one we did not come in by.
			int back = (dir + 2) % 4;
			int nextDir = -1;
			for (int d = 0; d < 4; ++d)
			{
				if (d != back && g.isBoundary(x, y, d))
				{
					nextDir = d;
					break;
				}
			}
			if (nextDir < 0)
				break;
			dir = nextDir;
		}
	}

	/// Douglas-Peucker between pts[first] and pts[last]: keep a point if it is more than num/den cells off the line.
	void simplifyRange( const Grid& g, const std::vector<int>& pts, int first, int last, I64 num, I64 den,
											std::vector<unsigned char>& keep )
	{
		std::vector<int> stack;
		stack.push_back(first);
		stack.push_back(last);
		while (!stack.empty())
		{
			int hi = stack.back(); stack.pop_back();
			int lo = stack.back(); stack.pop_back();
			if (hi - lo < 2)
				continue;
			Vert a = latticePoint(g, pts[lo]);
			Vert b = latticePoint(g, pts[hi]);
			I64 len2 = (I64)(b.x-a.x)*(b.x-a.x) + (I64)(b.y-a.y)*(b.y-a.y);
			int worst = -1;
			I64 worstVal = -1;
			for (int i = lo + 1; i < hi; ++i)
			{
				Vert p = latticePoint(g, pts[i]);
				I64 val;
				if (len2 == 0)
					val = (I64)(p.x-a.x)*(p.x-a.x) + (I64)(p.y-a.y)*(p.y-a.y);
				else
				{
					I64 cr = orient(a, b, p);
					val = cr * cr;
				}
				if (val > worstVal)
				{
					worstVal = val;
					worst = i;
				}
			}
			// distance^2 = cross^2 / len2 (or the plain distance^2 when the ends coincide); compare with (num/den)^2.
			bool off = (len2 == 0) ? (worstVal * den * den > num * num || worstVal > 0)
															: (worstVal * den * den > num * num * len2);
			// And whatever the tolerance: the straight line may not leave the centre of a cell beside the boundary on
			// the wrong side of it. That is what keeps the mesh and the grid in agreement about every cell (so about
			// what is connected to what) however much the boundary is straightened. In doubled coordinates the centres
			// of the two cells beside a unit edge are its midpoint moved one step along its left / right normal.
			if (!off && worstVal > 0 && len2 != 0)
			{
				Vert a2, b2;
				a2.x = a.x*2; a2.y = a.y*2;
				b2.x = b.x*2; b2.y = b.y*2;
				for (int i = lo; i < hi; ++i)
				{
					Vert p = latticePoint(g, pts[i]);
					Vert q = latticePoint(g, pts[i+1]);
					int dx = q.x - p.x, dy = q.y - p.y;
					Vert leftCentre, rightCentre;
					leftCentre.x = 2*p.x + dx - dy;  leftCentre.y = 2*p.y + dy + dx;
					rightCentre.x = 2*p.x + dx + dy; rightCentre.y = 2*p.y + dy - dx;
					if (orient(a2, b2, leftCentre) < 0 || orient(a2, b2, rightCentre) > 0)
					{
						off = true;
						break;
					}
				}
			}
			if (off)
			{
				keep[worst] = 1;
				stack.push_back(lo); stack.push_back(worst);
				stack.push_back(worst); stack.push_back(hi);
			}
		}
	}

	void simplifyChain( const Grid& g, Chain& chain, I64 num, I64 den )
	{
		int n = (int)chain.pts.size();
		std::vector<unsigned char> keep(n, 0);
		keep[0] = 1;
		keep[n-1] = 1;
		simplifyRange(g, chain.pts, 0, n-1, num, den, keep);
		chain.kept.clear();
		for (int i = 0; i < n; ++i)
		{
			if (keep[i])
				chain.kept.push_back(i);
		}
	}

	inline bool onSegment( const Vert& a, const Vert& b, const Vert& p )
	{
		// p is known collinear with a-b.
		return p.x >= (a.x < b.x ? a.x : b.x) && p.x <= (a.x > b.x ? a.x : b.x)
				&& p.y >= (a.y < b.y ? a.y : b.y) && p.y <= (a.y > b.y ? a.y : b.y);
	}

	/// Do two boundary segments meet anywhere other than at one shared end point?
	bool segmentsConflict( const Vert& a, const Vert& b, int ia, int ib, const Vert& c, const Vert& d, int ic, int id )
	{
		bool shareA = (ia == ic || ia == id);
		bool shareB = (ib == ic || ib == id);
		if (shareA && shareB)
			return true;
		if (shareA || shareB)
		{
			const Vert& s = shareA ? a : b;		// the shared point
			const Vert& o1 = shareA ? b : a;	// our other end
			const Vert& o2 = (shareA ? (ia == ic) : (ib == ic)) ? d : c;	// their other end
			return orient(s, o1, o2) == 0 && dotFrom(s, o1, o2) > 0;
		}
		I64 o1 = orient(a, b, c), o2 = orient(a, b, d), o3 = orient(c, d, a), o4 = orient(c, d, b);
		if (oppositeSides(o1, o2) && oppositeSides(o3, o4))
			return true;
		if (o1 == 0 && onSegment(a, b, c)) return true;
		if (o2 == 0 && onSegment(a, b, d)) return true;
		if (o3 == 0 && onSegment(c, d, a)) return true;
		if (o4 == 0 && onSegment(c, d, b)) return true;
		return false;
	}

	struct Seg
	{
		int a, b;		///< lattice vertex ids
		int chain;
	};
}

//-------------------------------------------------------------------------------------------------
void buildMesh( const unsigned char *labels, int width, int height, int simplifyNum, int simplifyDen, Mesh& out,
								unsigned exactLabels )
{
	Grid g;
	g.labels = labels;
	g.w = width;
	g.h = height;
	out.width = width;
	out.height = height;
	out.verts.clear();
	out.tris.clear();
	out.stats = Stats();
	if (simplifyDen <= 0)
		simplifyDen = 1;

	// ---- 1. Chains. A node is a lattice point where the boundary branches, ends, or turns a corner of the map.
	int numPts = (width+1) * (height+1);
	std::vector<unsigned char> isNode(numPts, 0);
	std::vector<unsigned char> degree(numPts, 0);
	int x, y, d;
	for (y = 0; y <= height; ++y)
	{
		for (x = 0; x <= width; ++x)
		{
			int deg = 0;
			for (d = 0; d < 4; ++d)
			{
				if (g.isBoundary(x, y, d))
					++deg;
			}
			degree[g.vid(x, y)] = (unsigned char)deg;
			bool corner = (x == 0 || x == width) && (y == 0 || y == height);
			if (deg != 0 && (deg != 2 || corner))
				isNode[g.vid(x, y)] = 1;
		}
	}
	std::vector<unsigned char> used((height+1)*width + height*(width+1), 0);
	std::vector<Chain> chains;
	for (y = 0; y <= height; ++y)
	{
		for (x = 0; x <= width; ++x)
		{
			if (!isNode[g.vid(x, y)])
				continue;
			for (d = 0; d < 4; ++d)
			{
				if (g.isBoundary(x, y, d) && !used[g.edgeId(x, y, d)])
				{
					chains.push_back(Chain());
					follow(g, isNode, used, x, y, d, chains.back());
				}
			}
		}
	}
	// What is left are closed loops with no node on them.
	for (y = 0; y <= height; ++y)
	{
		for (x = 0; x <= width; ++x)
		{
			if (degree[g.vid(x, y)] != 2 || isNode[g.vid(x, y)])
				continue;
			for (d = 0; d < 4; ++d)
			{
				if (g.isBoundary(x, y, d) && !used[g.edgeId(x, y, d)])
				{
					chains.push_back(Chain());
					follow(g, isNode, used, x, y, d, chains.back());
					break;	// the loop used both of this point's edges.
				}
			}
		}
	}
	// A chain that ends where it starts is cut in two at its farthest point, so that no chain has coincident ends.
	size_t numChains = chains.size();
	for (size_t c = 0; c < numChains; ++c)
	{
		if (chains[c].pts.front() != chains[c].pts.back())
			continue;
		Vert a = latticePoint(g, chains[c].pts[0]);
		int farIdx = 0;
		I64 farDist = -1;
		for (size_t i = 1; i + 1 < chains[c].pts.size(); ++i)
		{
			Vert p = latticePoint(g, chains[c].pts[i]);
			I64 dist = (I64)(p.x-a.x)*(p.x-a.x) + (I64)(p.y-a.y)*(p.y-a.y);
			if (dist > farDist)
			{
				farDist = dist;
				farIdx = (int)i;
			}
		}
		Chain second;
		second.left = chains[c].left;
		second.right = chains[c].right;
		second.exact = false;
		second.pts.assign(chains[c].pts.begin() + farIdx, chains[c].pts.end());
		chains[c].pts.resize(farIdx + 1);
		chains.push_back(second);
	}

	// ---- 2. Simplify; a chain whose simplified form touches another chain goes back to exact.
	out.stats.chains = (int)chains.size();
	for (size_t c = 0; c < chains.size(); ++c)
	{
		// A boundary two meshes are joined along stays where the grid has it.
		unsigned char other = chains[c].left == 0 ? chains[c].right : (chains[c].right == 0 ? chains[c].left : (unsigned char)0);
		bool keepExact = simplifyNum == 0 || (other != 0 && other < 32 && ((exactLabels >> other) & 1u) != 0);
		simplifyChain(g, chains[c], keepExact ? 0 : simplifyNum, keepExact ? 1 : simplifyDen);
		if (keepExact)
			chains[c].exact = true;
		out.stats.exactPoints += (int)chains[c].pts.size();
	}
	const int BUCKET = 8;
	int bw = width / BUCKET + 1, bh = height / BUCKET + 1;
	std::vector<Seg> segs;
	std::vector<std::vector<int> > buckets(bw * bh);
	for (int round = 0; simplifyNum != 0 && round < 64; ++round)
	{
		segs.clear();
		for (size_t b = 0; b < buckets.size(); ++b)
			buckets[b].clear();
		for (size_t c = 0; c < chains.size(); ++c)
		{
			for (size_t k = 0; k + 1 < chains[c].kept.size(); ++k)
			{
				Seg s;
				s.a = chains[c].pts[chains[c].kept[k]];
				s.b = chains[c].pts[chains[c].kept[k+1]];
				s.chain = (int)c;
				Vert a = latticePoint(g, s.a), b = latticePoint(g, s.b);
				int x0 = (a.x < b.x ? a.x : b.x) / BUCKET, x1 = (a.x > b.x ? a.x : b.x) / BUCKET;
				int y0 = (a.y < b.y ? a.y : b.y) / BUCKET, y1 = (a.y > b.y ? a.y : b.y) / BUCKET;
				for (int by = y0; by <= y1; ++by)
				{
					for (int bx = x0; bx <= x1; ++bx)
						buckets[by*bw + bx].push_back((int)segs.size());
				}
				segs.push_back(s);
			}
		}
		std::vector<unsigned char> revert(chains.size(), 0);
		bool any = false;
		for (size_t b = 0; b < buckets.size(); ++b)
		{
			const std::vector<int>& list = buckets[b];
			for (size_t i = 0; i < list.size(); ++i)
			{
				const Seg& s1 = segs[list[i]];
				Vert a1 = latticePoint(g, s1.a), b1 = latticePoint(g, s1.b);
				for (size_t j = i + 1; j < list.size(); ++j)
				{
					const Seg& s2 = segs[list[j]];
					if (chains[s1.chain].exact && chains[s2.chain].exact)
						continue;
					Vert a2 = latticePoint(g, s2.a), b2 = latticePoint(g, s2.b);
					if (segmentsConflict(a1, b1, s1.a, s1.b, a2, b2, s2.a, s2.b))
					{
						if (!chains[s1.chain].exact) { revert[s1.chain] = 1; any = true; }
						if (!chains[s2.chain].exact) { revert[s2.chain] = 1; any = true; }
					}
				}
			}
		}
		if (!any)
			break;
		for (size_t c = 0; c < chains.size(); ++c)
		{
			if (revert[c])
			{
				simplifyChain(g, chains[c], 0, 1);
				chains[c].exact = true;
				++out.stats.revertedChains;
			}
		}
	}

	// ---- 3. Triangulate.
	CDT cdt;
	cdt.init(width, height);
	std::vector<int> vertOf(numPts, -1);
	for (size_t c = 0; c < chains.size(); ++c)
	{
		out.stats.keptPoints += (int)chains[c].kept.size();
		for (size_t k = 0; k < chains[c].kept.size(); ++k)
		{
			int id = chains[c].pts[chains[c].kept[k]];
			if (vertOf[id] < 0)
			{
				Vert p = latticePoint(g, id);
				vertOf[id] = cdt.addVertex(p.x, p.y);
			}
		}
	}
	for (size_t c = 0; c < chains.size(); ++c)
	{
		for (size_t k = 0; k + 1 < chains[c].kept.size(); ++k)
			cdt.addConstraint(vertOf[chains[c].pts[chains[c].kept[k]]], vertOf[chains[c].pts[chains[c].kept[k+1]]]);
	}

	// ---- 4. Labels: seed the triangles on each side of every boundary segment, then fill each region.
	std::vector<int> queue;
	for (size_t c = 0; c < chains.size(); ++c)
	{
		for (size_t k = 0; k + 1 < chains[c].kept.size(); ++k)
		{
			int u = vertOf[chains[c].pts[chains[c].kept[k]]];
			int v = vertOf[chains[c].pts[chains[c].kept[k+1]]];
			for (int side = 0; side < 2; ++side)
			{
				unsigned char label = side == 0 ? chains[c].left : chains[c].right;
				if (label == OUTSIDE)
					continue;
				int e = side == 0 ? cdt.findEdge(u, v) : cdt.findEdge(v, u);
				if (e < 0)
					continue;	// the segment was cut at a vertex lying on it: its pieces get their label from the fill.
				Tri& tr = cdt.tris[e/3];
				if (tr.label == LABEL_NONE)
				{
					tr.label = label;
					queue.push_back(e/3);
				}
				else if (tr.label != label)
					++out.stats.labelConflicts;
			}
		}
	}
	if (queue.empty() && !cdt.tris.empty())
	{
		// One label over the whole map and no boundary inside it.
		cdt.tris[0].label = g.cell(0, 0);
		queue.push_back(0);
	}
	for (size_t q = 0; q < queue.size(); ++q)
	{
		int t = queue[q];
		for (int i = 0; i < 3; ++i)
		{
			int nb = cdt.tris[t].n[i];
			if (nb < 0 || cdt.tris[t].c[i])
				continue;
			if (cdt.tris[nb].label == LABEL_NONE)
			{
				cdt.tris[nb].label = cdt.tris[t].label;
				queue.push_back(nb);
			}
			else if (cdt.tris[nb].label != cdt.tris[t].label)
				++out.stats.labelConflicts;
		}
	}
	for (size_t t = 0; t < cdt.tris.size(); ++t)
	{
		if (cdt.tris[t].label == LABEL_NONE)
			++out.stats.unlabelled;
		// The map edge counts as a boundary.
		for (int i = 0; i < 3; ++i)
		{
			if (cdt.tris[t].n[i] < 0)
				cdt.tris[t].c[i] = 1;
		}
	}
	out.verts.swap(cdt.verts);
	out.tris.swap(cdt.tris);
}

//-------------------------------------------------------------------------------------------------
namespace
{
	// mapEdges: bit 0 left, 1 bottom, 2 right, 3 top: that side of the grid is the edge of the map, which a unit keeps
	// its radius away from like a wall. A side without its bit is only the edge of the window being looked at.
	void erodeImpl( const unsigned char *labels, int width, int height, unsigned passMask, int radiusHalfCells,
									unsigned mapEdges, std::vector<unsigned char>& out )
	{
		int W = width * 2, H = height * 2;
		int R = radiusHalfCells < 0 ? 0 : radiusHalfCells;
		std::vector<unsigned char> wall(W * H);
		int x, y;
		for (y = 0; y < H; ++y)
		{
			for (x = 0; x < W; ++x)
			{
				unsigned char label = labels[(y/2)*width + (x/2)];
				wall[y*W + x] = (label < 32 && ((passMask >> label) & 1u)) ? 0 : 1;
			}
		}
		out = wall;
		if (R == 0)
			return;
		I64 limit = (I64)(2*R) * (2*R);
		for (y = 0; y < H; ++y)
		{
			for (x = 0; x < W; ++x)
			{
				if (((mapEdges & 1u) && x < R) || ((mapEdges & 2u) && y < R) || ((mapEdges & 4u) && x >= W - R)
						|| ((mapEdges & 8u) && y >= H - R))
					out[y*W + x] = 1;	// too near the map edge
				if (!wall[y*W + x])
					continue;
				// Only a wall half-cell with open ground beside it can be the nearest wall to anything.
				bool edge = false;
				for (int ny = y - 1; ny <= y + 1 && !edge; ++ny)
				{
					for (int nx = x - 1; nx <= x + 1; ++nx)
					{
						if (nx >= 0 && ny >= 0 && nx < W && ny < H && !wall[ny*W + nx])
						{
							edge = true;
							break;
						}
					}
				}
				if (!edge)
					continue;
				for (int dy = -R; dy <= R; ++dy)
				{
					int ty = y + dy;
					if (ty < 0 || ty >= H)
						continue;
					// Distance from the centre of the target half-cell to this wall half-cell's square, in quarter-cells.
					I64 ey = dy == 0 ? 0 : 2 * (dy < 0 ? -dy : dy) - 1;
					for (int dx = -R; dx <= R; ++dx)
					{
						int tx = x + dx;
						if (tx < 0 || tx >= W)
							continue;
						I64 ex = dx == 0 ? 0 : 2 * (dx < 0 ? -dx : dx) - 1;
						if (ex*ex + ey*ey < limit)
							out[ty*W + tx] = 1;
					}
				}
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
void erode( const unsigned char *labels, int width, int height, unsigned passMask, int radiusHalfCells,
						std::vector<unsigned char>& out )
{
	erodeImpl(labels, width, height, passMask, radiusHalfCells, 15u, out);
}

//-------------------------------------------------------------------------------------------------
void erodeSheet( const unsigned char *labels, const unsigned char *deck, int width, int height, unsigned passMask,
								 int radiusHalfCells, const int pad[4], unsigned mapEdges, std::vector<unsigned char>& out )
{
	std::vector<unsigned char> all;
	erodeImpl(labels, width, height, passMask, radiusHalfCells, mapEdges, all);
	int W = width * 2;
	int x0 = pad[0] * 2, y0 = pad[1] * 2;
	int w = (width - pad[0] - pad[2]) * 2, h = (height - pad[1] - pad[3]) * 2;
	if (w <= 0 || h <= 0)
	{
		out.clear();
		return;
	}
	out.resize(w * h);
	for (int y = 0; y < h; ++y)
	{
		for (int x = 0; x < w; ++x)
		{
			unsigned char v = all[(y + y0)*W + (x + x0)];
			if (v == 0 && deck[((y + y0)/2)*width + ((x + x0)/2)])
				v = SHEET_DECK;
			out[y*w + x] = v;
		}
	}
}

//-------------------------------------------------------------------------------------------------
void assignSheets( std::vector<SheetCells>& sheets )
{
	if (sheets.empty())
		return;
	const int W = sheets[0].w, H = sheets[0].h;
	const int NOBODY = 255, FAR_AWAY = 0x3fffffff;
	std::vector<unsigned char> owner(W * H, (unsigned char)NOBODY);
	std::vector<int> ownerDist(W * H, FAR_AWAY);
	std::vector<int> distToDeck, queue;
	size_t k;
	int x, y;
	for (k = 1; k < sheets.size() && k < (size_t)NOBODY; ++k)
	{
		const SheetCells& sh = sheets[k];
		// Steps (8 directions) from each half-cell of the window to the nearest half-cell of this bridge's deck.
		distToDeck.assign(sh.w * sh.h, FAR_AWAY - 1);
		queue.clear();
		for (int i = 0; i < sh.w * sh.h; ++i)
		{
			if (sh.cells[i] == SHEET_DECK)
			{
				distToDeck[i] = 0;
				queue.push_back(i);
			}
		}
		for (size_t q = 0; q < queue.size(); ++q)
		{
			int c = queue[q];
			int cx = c % sh.w, cy = c / sh.w;
			for (int dy = -1; dy <= 1; ++dy)
			{
				for (int dx = -1; dx <= 1; ++dx)
				{
					int nx = cx + dx, ny = cy + dy;
					if (nx < 0 || ny < 0 || nx >= sh.w || ny >= sh.h || distToDeck[ny*sh.w + nx] <= distToDeck[c] + 1)
						continue;
					distToDeck[ny*sh.w + nx] = distToDeck[c] + 1;
					queue.push_back(ny*sh.w + nx);
				}
			}
		}
		for (y = 0; y < sh.h; ++y)
		{
			for (x = 0; x < sh.w; ++x)
			{
				int gx = x + sh.ox, gy = y + sh.oy;
				if (sh.cells[y*sh.w + x] != SHEET_GROUND || gx < 0 || gy < 0 || gx >= W || gy >= H)
					continue;
				if (distToDeck[y*sh.w + x] < ownerDist[gy*W + gx])	// a tie stays with the earlier bridge
				{
					ownerDist[gy*W + gx] = distToDeck[y*sh.w + x];
					owner[gy*W + gx] = (unsigned char)k;
				}
			}
		}
	}
	for (int i = 0; i < W * H; ++i)
	{
		if (owner[i] == NOBODY && sheets[0].cells[i] == SHEET_GROUND)
			owner[i] = 0;
		sheets[0].cells[i] = (unsigned char)(owner[i] == 0 ? SHEET_GROUND : (owner[i] == NOBODY ? SHEET_WALL : SHEET_OTHER));
	}
	for (k = 1; k < sheets.size(); ++k)
	{
		SheetCells& sh = sheets[k];
		std::vector<unsigned char> grown((sh.w + 2) * (sh.h + 2));
		for (y = -1; y <= sh.h; ++y)
		{
			for (x = -1; x <= sh.w; ++x)
			{
				int gx = x + sh.ox, gy = y + sh.oy;
				unsigned char v = SHEET_WALL;
				bool inWindow = (x >= 0 && y >= 0 && x < sh.w && y < sh.h);
				if (inWindow && sh.cells[y*sh.w + x] == SHEET_DECK)
					v = SHEET_DECK;
				else if (gx >= 0 && gy >= 0 && gx < W && gy < H && owner[gy*W + gx] != NOBODY)
					v = (unsigned char)((inWindow && owner[gy*W + gx] == k) ? SHEET_GROUND : SHEET_OTHER);
				grown[(y + 1)*(sh.w + 2) + (x + 1)] = v;
			}
		}
		sh.cells.swap(grown);
		sh.ox -= 1;
		sh.oy -= 1;
		sh.w += 2;
		sh.h += 2;
	}
}

//-------------------------------------------------------------------------------------------------
int validate( const Mesh& mesh, std::vector<char>& report )
{
	int bad = 0;
	char line[160];
	report.clear();
#define NAV_FAIL(...) do { if (bad < 12) { sprintf(line, __VA_ARGS__); report.insert(report.end(), line, line + strlen(line)); report.push_back('\n'); } ++bad; } while (0)
	I64 area2 = 0;
	for (size_t t = 0; t < mesh.tris.size(); ++t)
	{
		const Tri& tr = mesh.tris[t];
		I64 a = orient(mesh.verts[tr.v[0]], mesh.verts[tr.v[1]], mesh.verts[tr.v[2]]);
		if (a <= 0)
			NAV_FAIL("tri %d: not counter-clockwise (area2 %lld)", (int)t, a);
		area2 += a;
		for (int i = 0; i < 3; ++i)
		{
			int nb = tr.n[i];
			if (nb < 0)
			{
				const Vert& p = mesh.verts[tr.v[i]];
				const Vert& q = mesh.verts[tr.v[(i+1)%3]];
				bool onEdge = (p.x == q.x && (p.x == 0 || p.x == mesh.width)) || (p.y == q.y && (p.y == 0 || p.y == mesh.height));
				if (!onEdge)
					NAV_FAIL("tri %d edge %d: no neighbour but not on the map edge", (int)t, i);
				continue;
			}
			const Tri& o = mesh.tris[nb];
			int j = -1;
			for (int k = 0; k < 3; ++k)
			{
				if (o.n[k] == (int)t && o.v[k] == tr.v[(i+1)%3] && o.v[(k+1)%3] == tr.v[i])
					j = k;
			}
			if (j < 0)
			{
				NAV_FAIL("tri %d edge %d: neighbour %d does not point back", (int)t, i, nb);
				continue;
			}
			if (o.c[j] != tr.c[i])
				NAV_FAIL("tri %d edge %d: constraint flag differs from neighbour's", (int)t, i);
			if (!tr.c[i])
			{
				if (o.label != tr.label)
					NAV_FAIL("tri %d edge %d: labels %d and %d meet without a boundary", (int)t, i, tr.label, o.label);
				if (inCircle(mesh.verts[tr.v[0]], mesh.verts[tr.v[1]], mesh.verts[tr.v[2]], mesh.verts[o.v[(j+2)%3]]) > 0)
					NAV_FAIL("tri %d edge %d: not Delaunay", (int)t, i);
			}
			else if (o.label == tr.label)
				NAV_FAIL("tri %d edge %d: a boundary with label %d on both sides", (int)t, i, tr.label);
		}
		if (tr.label == LABEL_NONE)
			NAV_FAIL("tri %d: no label", (int)t);
	}
	if (area2 != (I64)2 * mesh.width * mesh.height)
		NAV_FAIL("area: triangles cover %lld half-cells, the map has %lld", area2, (I64)2 * mesh.width * mesh.height);
	if (mesh.stats.labelConflicts)
		NAV_FAIL("%d label conflicts while filling regions", mesh.stats.labelConflicts);
#undef NAV_FAIL
	report.push_back(0);
	return bad;
}

}	// namespace NavBuild
