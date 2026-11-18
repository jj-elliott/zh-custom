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

// zh-pipeline @feature 2026-11-17 The drive plan, see NavDrive.h.

#ifndef NAVMESH_STANDALONE
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine
#endif

#include "GameLogic/NavDrive.h"

#include <math.h>

namespace NavDrive
{

namespace
{
	const float PI_F = 3.14159265f;
	const float TWO_PI_F = 6.2831853f;
	const float BIG = 1.0e30f;

	inline float dist( const Pt& a, const Pt& b )
	{
		float dx = b.x - a.x, dy = b.y - a.y;
		return (float)sqrt(dx*dx + dy*dy);
	}
	/// a brought into (-pi, pi]
	float wrap( float a )
	{
		while (a > PI_F) a -= TWO_PI_F;
		while (a <= -PI_F) a += TWO_PI_F;
		return a;
	}

	/// Something the line goes to, or round: the start, a corner, the goal.
	struct Post
	{
		Pt at;				///< the path's own point
		Pt c;					///< centre of the turning circle (= at when there is none)
		Pt inward;		///< unit vector from 'at' to the inside of the turn
		float rho;		///< radius of the circle, 0 = none
		float side;		///< +rho: the centre is on the left of travel (a left turn), -rho: on the right
		float full;		///< the radius the unit would like here
		int shrinks;
		bool startCircle;	///< a wheeled unit's turning circle at the start: it is already on it, at 'at'
	};

	void place( Post& p )
	{
		p.c.x = p.at.x + p.inward.x * p.rho;
		p.c.y = p.at.y + p.inward.y * p.rho;
		p.side = p.side >= 0.0f ? p.rho : -p.rho;
	}

	/// The straight line leaving post a and arriving at post b with each circle on its side: n = the line's left
	/// normal. False if the circles leave no room for it.
	bool tangent( const Post& a, const Post& b, Pt& n )
	{
		float dx = b.c.x - a.c.x, dy = b.c.y - a.c.y;
		float len = (float)sqrt(dx*dx + dy*dy);
		float diff = b.side - a.side;
		if (len < 1.0e-4f || (diff > len * 0.999f) || (diff < -len * 0.999f))
			return false;
		float ux = dx / len, uy = dy / len;
		float k = diff / len;
		float m = (float)sqrt(1.0f - k*k);
		n.x = ux * k - uy * m;
		n.y = uy * k + ux * m;
		return true;
	}

	/// How far round the circle from angle a0 to angle a1, going the way the unit turns (0 .. 2 pi).
	float sweepOf( float a0, float a1, float side )
	{
		float s = (side > 0.0f) ? (a1 - a0) : (a0 - a1);
		while (s < 0.0f) s += TWO_PI_F;
		while (s >= TWO_PI_F) s -= TWO_PI_F;
		if (s > TWO_PI_F - 0.02f)
			s = 0.0f;	// rounding made "no turn" look like a full circle.
		return s;
	}

	void pose( const Prim& p, float along, float& x, float& y, float& heading )
	{
		if (p.type == LINE)
		{
			float t = p.length > 0.0f ? along / p.length : 1.0f;
			x = p.x0 + (p.x1 - p.x0) * t;
			y = p.y0 + (p.y1 - p.y0) * t;
			heading = (float)atan2(p.y1 - p.y0, p.x1 - p.x0);
		}
		else if (p.type == ARC)
		{
			float t = p.length > 0.0f ? along / p.length : 1.0f;
			float a = p.a0 + p.sweep * t;
			x = p.cx + p.r * (float)cos(a);
			y = p.cy + p.r * (float)sin(a);
			heading = wrap(a + (p.sweep >= 0.0f ? PI_F * 0.5f : -PI_F * 0.5f));
		}
		else
		{
			x = p.x0;
			y = p.y0;
			float done = along;
			float mag = p.turn >= 0.0f ? p.turn : -p.turn;
			if (done > mag)
				done = mag;
			heading = wrap(p.h0 + (p.turn >= 0.0f ? done : -done));
		}
	}
}

//-------------------------------------------------------------------------------------------------
void build( const std::vector<Pt>& pts, float heading, const Kinematics& kin, Plan& out )
{
	out.prims.clear();
	out.length = 0.0f;
	out.smallerArcs = 0;
	out.pivots = 0;
	out.cheats = 0;
	if (pts.size() < 2 || kin.turnRate <= 0.0f)
		return;

	bool wheels = kin.turnSpeed > 0.0f;
	float fullRadius = wheels ? kin.turnSpeed / kin.turnRate : kin.cornerSpeed / kin.turnRate;

	// ---- The posts. Points closer together than a hair are one point.
	std::vector<Pt> p;
	p.push_back(pts[0]);
	for (size_t i = 1; i < pts.size(); ++i)
	{
		if (dist(p.back(), pts[i]) > 0.01f || i + 1 == pts.size())
			p.push_back(pts[i]);
	}
	if (p.size() < 2 || dist(p[0], p[p.size()-1]) < 0.01f && p.size() == 2)
		return;
	size_t m = p.size();
	std::vector<Post> posts(m);
	for (size_t i = 0; i < m; ++i)
	{
		Post& q = posts[i];
		q.at = p[i];
		q.c = p[i];
		q.inward.x = 0.0f; q.inward.y = 0.0f;
		q.rho = 0.0f;
		q.side = 0.0f;
		q.full = 0.0f;
		q.shrinks = 0;
		q.startCircle = false;
		if (i == 0 || i + 1 == m)
			continue;
		float inLen = dist(p[i-1], p[i]), outLen = dist(p[i], p[i+1]);
		if (inLen < 1.0e-4f || outLen < 1.0e-4f)
			continue;
		float ix = (p[i].x - p[i-1].x) / inLen, iy = (p[i].y - p[i-1].y) / inLen;
		float ox = (p[i+1].x - p[i].x) / outLen, oy = (p[i+1].y - p[i].y) / outLen;
		float cr = ix * oy - iy * ox;
		float bx = ox - ix, by = oy - iy;	// points to the inside of the turn
		float bl = (float)sqrt(bx*bx + by*by);
		if (bl < 1.0e-3f)
		{
			q.shrinks = -1;	// straight on: not a corner at all, taken out below.
			continue;
		}
		if (cr == 0.0f)
			continue;	// straight back: no inside. The unit will pivot there.
		q.inward.x = bx / bl;
		q.inward.y = by / bl;
		q.side = cr > 0.0f ? 1.0f : -1.0f;
		q.rho = fullRadius;
		q.full = fullRadius;
		place(q);
	}
	for (size_t i = posts.size() - 1; i-- > 1; )
	{
		if (posts[i].shrinks < 0)
			posts.erase(posts.begin() + i);
	}
	m = posts.size();
	// A wheeled unit not already pointing along its first stretch starts on its turning circle.
	float h0x = (float)cos(heading), h0y = (float)sin(heading);
	if (wheels)
	{
		float tx = posts[1].at.x - p[0].x, ty = posts[1].at.y - p[0].y;
		float cr = h0x * ty - h0y * tx;
		float dt = h0x * tx + h0y * ty;
		float off = (float)atan2(cr, dt);
		if (off > 0.02f || off < -0.02f)
		{
			Post& q = posts[0];
			q.side = cr >= 0.0f ? 1.0f : -1.0f;
			q.inward.x = -h0y * q.side;	// the left normal of the heading for a left turn, the right one for a right
			q.inward.y = h0x * q.side;
			q.rho = fullRadius;
			q.full = fullRadius;
			q.startCircle = true;
			place(q);
		}
	}

	// ---- Fit. Swinging round one corner moves the straights either side of it, and that can do two things to a
	// neighbouring corner:
	//   - leave it bending the other way, or not at all: the line now passes outside that corner, which therefore
	//     no longer shapes it. It is dropped (a unit must not turn left to pass a corner the path turns right at);
	//   - leave no room for its circle, or ask more than a half turn of it (the corners are too close together):
	//     that circle is made smaller, and in the end removed, which means stop and pivot.
	std::vector<Pt> normals;
	for (int round = 0; round < 64; ++round)
	{
		m = posts.size();
		normals.resize(m - 1);
		bool allGood = true;
		std::vector<unsigned char> bad(m, 0);
		for (size_t k = 0; k + 1 < m; ++k)
		{
			if (!tangent(posts[k], posts[k+1], normals[k]))
			{
				// shrink the bigger of the two
				size_t which = posts[k].rho >= posts[k+1].rho ? k : k + 1;
				if (posts[which].rho <= 0.0f)
					which = (which == k) ? k + 1 : k;
				bad[which] = 1;
				allGood = false;
			}
		}
		int drop = -1;
		if (allGood)
		{
			for (size_t i = 1; i + 1 < m && drop < 0; ++i)
			{
				const Post& q = posts[i];
				float turn;	// how far the line turns here, counted the way the path turns at this corner
				if (q.rho > 0.0f)
				{
					float aIn = (float)atan2(-normals[i-1].y * q.side, -normals[i-1].x * q.side);
					float aOut = (float)atan2(-normals[i].y * q.side, -normals[i].x * q.side);
					turn = sweepOf(aIn, aOut, q.side);
				}
				else if (q.side != 0.0f)
				{
					float dirIn = (float)atan2(-normals[i-1].x, normals[i-1].y);
					float dirOut = (float)atan2(-normals[i].x, normals[i].y);
					turn = wrap(dirOut - dirIn) * (q.side > 0.0f ? 1.0f : -1.0f);
					if (turn < 0.0f)
						turn += TWO_PI_F;
				}
				else
					continue;	// a point the path goes straight through, or straight back at
				// Straight on, or bending a little the other way, at a corner the straightened line still passes
				// close to: that corner no longer shapes the line. (A big bend the other way is not this: it is two
				// corners too close together, a hairpin, and those keep their corners and lose their circles.)
				bool slight = turn < 0.004f || turn > TWO_PI_F - 0.35f;
				bool closeBy = false;
				if (slight)
				{
					const Pt& a = posts[i-1].c;
					const Pt& b = posts[i+1].c;
					float len2 = (b.x-a.x)*(b.x-a.x) + (b.y-a.y)*(b.y-a.y);
					float t = len2 > 0.0f ? ((q.at.x-a.x)*(b.x-a.x) + (q.at.y-a.y)*(b.y-a.y)) / len2 : 0.0f;
					if (t < 0.0f) t = 0.0f;
					if (t > 1.0f) t = 1.0f;
					Pt on;
					on.x = a.x + (b.x-a.x)*t;
					on.y = a.y + (b.y-a.y)*t;
					closeBy = dist(q.at, on) <= fullRadius + 0.5f;
				}
				if (slight && closeBy)
					drop = (int)i;
				else if (turn > PI_F + 0.3f)
				{
					// More than a half turn, i.e. really a turn the other way: this corner and its neighbours are too
					// close together for their circles. Make them smaller; with none left the line is the path's own
					// string, which turns exactly where and as the path does.
					bool any = false;
					for (size_t n = i - 1; n <= i + 1; ++n)
					{
						if (posts[n].rho > 0.0f)
						{
							bad[n] = 1;
							any = true;
						}
					}
					if (any)
						allGood = false;
				}
			}
		}
		if (drop >= 0)
		{
			posts.erase(posts.begin() + drop);
			continue;
		}
		if (allGood)
			break;
		for (size_t i = 0; i < m; ++i)
		{
			if (!bad[i] || posts[i].rho <= 0.0f)
				continue;
			Post& q = posts[i];
			++q.shrinks;
			q.rho = (q.shrinks >= 3) ? 0.0f : q.rho * 0.5f;
			float keep = q.side;
			place(q);
			if (q.rho <= 0.0f)
			{
				q.c = q.at;
				q.side = keep > 0.0f ? 1.0e-9f : -1.0e-9f;	// no circle, but remember which way the path turns here
			}
		}
	}
	m = posts.size();
	normals.resize(m - 1);
	// Whatever is still impossible (cannot happen once every circle is gone, but be safe): no circles at all.
	for (size_t k = 0; k + 1 < m; ++k)
	{
		if (!tangent(posts[k], posts[k+1], normals[k]))
		{
			for (size_t i = 0; i < m; ++i)
			{
				posts[i].rho = 0.0f;
				posts[i].side = 0.0f;
				posts[i].c = posts[i].at;
			}
			for (size_t j = 0; j + 1 < m; ++j)
			{
				if (!tangent(posts[j], posts[j+1], normals[j]))
				{
					normals[j].x = 0.0f;
					normals[j].y = 1.0f;
				}
			}
			break;
		}
	}

	// ---- Lay it out.
	float cur = heading;
	for (size_t k = 0; k + 1 < m; ++k)
	{
		const Post& a = posts[k];
		const Post& b = posts[k+1];
		const Pt& n = normals[k];
		Pt leave, arrive;
		leave.x = a.c.x - n.x * a.side;  leave.y = a.c.y - n.y * a.side;
		arrive.x = b.c.x - n.x * b.side; arrive.y = b.c.y - n.y * b.side;
		float dir = (float)atan2(-n.x, n.y);	// the direction whose left normal is n

		if (a.rho > 0.0f)
		{
			Prim arc;
			arc.type = ARC;
			arc.cx = a.c.x; arc.cy = a.c.y; arc.r = a.rho;
			float aOut = (float)atan2(leave.y - a.c.y, leave.x - a.c.x);
			float aIn;
			if (a.startCircle)
				aIn = (float)atan2(a.at.y - a.c.y, a.at.x - a.c.x);
			else
				aIn = (float)atan2(-normals[k-1].y * a.side, -normals[k-1].x * a.side);
			float sw = sweepOf(aIn, aOut, a.side);
			arc.a0 = aIn;
			arc.sweep = a.side > 0.0f ? sw : -sw;
			arc.length = sw * a.rho;
			if (wheels)
				arc.vmax = kin.turnSpeed * (a.rho / a.full);
			else
				arc.vmax = kin.turnRate * a.rho;
			if (arc.vmax > kin.maxSpeed)
				arc.vmax = kin.maxSpeed;
			arc.x0 = arc.y0 = arc.x1 = arc.y1 = arc.h0 = arc.turn = 0.0f;
			if (a.rho < a.full)
			{
				++out.smallerArcs;
				if (wheels)
					++out.cheats;
			}
			if (sw > 1.0e-4f)
				out.prims.push_back(arc);
		}
		else
		{
			// No circle here: if the line onward is not the way the unit points, it stops and turns on the spot.
			float turn = wrap(dir - cur);
			if (turn > 0.004f || turn < -0.004f)
			{
				Prim pv;
				pv.type = PIVOT;
				pv.x0 = a.at.x; pv.y0 = a.at.y;
				pv.h0 = cur;
				pv.turn = turn;
				pv.length = 0.0f;
				pv.vmax = 0.0f;
				pv.x1 = pv.y1 = pv.cx = pv.cy = pv.r = pv.a0 = pv.sweep = 0.0f;
				out.prims.push_back(pv);
				if (k > 0)
					++out.pivots;
				if (wheels)
					++out.cheats;
			}
		}
		Prim ln;
		ln.type = LINE;
		ln.x0 = leave.x; ln.y0 = leave.y; ln.x1 = arrive.x; ln.y1 = arrive.y;
		ln.length = dist(leave, arrive);
		ln.vmax = kin.maxSpeed;
		ln.cx = ln.cy = ln.r = ln.a0 = ln.sweep = ln.h0 = ln.turn = 0.0f;
		if (ln.length > 1.0e-3f)
			out.prims.push_back(ln);
		cur = dir;
	}
	for (size_t i = 0; i < out.prims.size(); ++i)
		out.length += out.prims[i].length;
}

//-------------------------------------------------------------------------------------------------
void start( const Plan& plan, float x, float y, float heading, float speed, State& st )
{
	st.prim = 0;
	st.along = 0.0f;
	st.v = speed;
	st.x = x;
	st.y = y;
	st.heading = heading;
	st.moved = 0.0f;
	st.done = plan.prims.empty();
	if (st.done)
		st.v = 0.0f;
}

//-------------------------------------------------------------------------------------------------
void step( const Plan& plan, const Kinematics& kin, float speedCap, State& st )
{
	if (st.done)
	{
		st.v = 0.0f;
		st.moved = 0.0f;
		return;
	}
	int count = (int)plan.prims.size();
	const Prim *p = &plan.prims[st.prim];

	if (p->type == PIVOT)
	{
		st.v = 0.0f;
		st.moved = 0.0f;
		float mag = p->turn >= 0.0f ? p->turn : -p->turn;
		st.along += kin.turnRate;
		if (st.along >= mag)
		{
			st.along = mag;
			pose(*p, st.along, st.x, st.y, st.heading);
			++st.prim;
			st.along = 0.0f;
			if (st.prim >= count)
				st.done = true;
			return;
		}
		pose(*p, st.along, st.x, st.y, st.heading);
		return;
	}

	// The most speed that still lets the brakes meet every limit ahead: each arc's, a pivot's stop, the end.
	float allowed = BIG;
	{
		float d = p->length - st.along;
		float horizon = kin.brake > 0.0f ? kin.maxSpeed * kin.maxSpeed / (2.0f * kin.brake) + 2.0f * kin.maxSpeed : BIG;
		for (int j = st.prim + 1; ; ++j)
		{
			float vq = (j >= count || plan.prims[j].type == PIVOT) ? 0.0f : plan.prims[j].vmax;
			// Time is in whole frames. Braking from speed u, a frame at a time (u, u - brake, u - 2 brake, ...), covers
			// S(u) = u^2 / (2 brake) + u / 2 before it is down to nothing. So the most speed from which the brakes can
			// still be down to vq when the limit is reached, d away, is the u with S(u) = d + S(vq).
			// (Be down to vq one frame's travel before the limit, so that the frame which crosses into a slower arc
			// is itself at the arc's speed and does not turn faster than the unit can.)
			float room = (d > vq ? d - vq : 0.0f) + vq*vq / (2.0f * kin.brake) + vq * 0.5f;
			float most = (-kin.brake + (float)sqrt(kin.brake * kin.brake + 8.0f * kin.brake * room)) * 0.5f;
			if (most < vq)
				most = vq;
			if (most < allowed)
				allowed = most;
			if (j >= count || plan.prims[j].type == PIVOT)
				break;
			d += plan.prims[j].length;
			if (d > horizon)
				break;
		}
	}
	float v = st.v + kin.accel;
	if (v > p->vmax) v = p->vmax;
	if (v > allowed) v = allowed;
	if (v > speedCap) v = speedCap;
	if (v < st.v - kin.brake) v = st.v - kin.brake;	// brakes are only so good
	if (v < 0.0f) v = 0.0f;

	st.moved = 0.0f;
	float move = v;
	while (move > 0.0f)
	{
		float rem = p->length - st.along;
		if (move < rem)
		{
			st.along += move;
			st.moved += move;
			move = 0.0f;
			break;
		}
		move -= rem;
		st.moved += rem;
		++st.prim;
		st.along = 0.0f;
		if (st.prim >= count)
		{
			// The end of the line.
			const Prim& last = plan.prims[count - 1];
			pose(last, last.length, st.x, st.y, st.heading);
			st.v = 0.0f;
			st.done = true;
			return;
		}
		p = &plan.prims[st.prim];
		if (p->type == PIVOT)
		{
			// Stop here and turn; what was left of this frame's travel is given up.
			st.x = p->x0;
			st.y = p->y0;
			st.heading = wrap(p->h0);
			st.v = 0.0f;
			return;
		}
	}
	st.v = v;
	pose(*p, st.along, st.x, st.y, st.heading);
}

}	// namespace NavDrive
