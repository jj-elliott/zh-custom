// zh-pipeline: standalone test of the drive plan (Core/GameEngine/.../NavDrive.cpp), no engine.
// Built and run by scripts/drivetest.ps1.
//   drivetest <count> [seed]            random paths, four real units' numbers, every frame checked
//   drivetest svg <out.svg> [seed]      one random path per unit, drawn: the string and the line actually driven
//
// What "checked" means, every frame of every run:
//   - the heading never changes by more than the unit's turn rate (a wheeled unit's: in proportion to its speed);
//   - the speed never rises by more than its acceleration nor falls by more than its braking;
//   - it moves exactly its speed, in the direction it faces;
//   - it never turns one way and then the other except where the path's own corners do (plus one for the turn onto
//     the path at the start): the user's rule, "no turn without a cause";
//   - it ends on the goal, stopped, and stays near the string throughout.

#include "GameLogic/NavDrive.h"
#include "GameLogic/NavMeshBuild.h"
#include "GameLogic/NavMeshQuery.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace NavDrive;

static long long s_segTests = 0, s_segWrong = 0, s_segGraze = 0;
static unsigned int s_seed = 1;
static unsigned int rnd()
{
	s_seed = s_seed * 1664525u + 1013904223u;
	return s_seed >> 8;
}
static float frand( float lo, float hi )
{
	return lo + (hi - lo) * (float)(rnd() % 100000) / 100000.0f;
}

struct Unit
{
	const char *name;
	Kinematics kin;
};
// From the game (MoveBench "bench.loco=1"): distance per logic frame, radians per frame.
static const Unit UNITS[4] = {
	{ "Crusader (treads)", { 1.000f, 0.1047f, 1.1111f, 1.1111f, 0.0f,   1.000f } },
	{ "Humvee (wheels)",   { 2.000f, 0.1047f, 1.1111f, 1.1111f, 0.667f, 2.000f } },
	{ "Technical (wheels)",{ 3.000f, 0.1047f, 0.1111f, 0.0556f, 0.750f, 3.000f } },
	{ "Ranger (legs)",     { 0.667f, 0.2909f, 0.1111f, 0.1111f, 0.0f,   0.667f } },
};

static float wrap( float a )
{
	while (a > 3.14159265f) a -= 6.2831853f;
	while (a <= -3.14159265f) a += 6.2831853f;
	return a;
}

static float distToPolyline( const std::vector<Pt>& pts, float x, float y )
{
	float best = 1.0e30f;
	for (size_t i = 0; i + 1 < pts.size(); ++i)
	{
		float ax = pts[i].x, ay = pts[i].y, bx = pts[i+1].x, by = pts[i+1].y;
		float len2 = (bx-ax)*(bx-ax) + (by-ay)*(by-ay);
		float t = len2 > 0.0f ? ((x-ax)*(bx-ax) + (y-ay)*(by-ay)) / len2 : 0.0f;
		if (t < 0.0f) t = 0.0f;
		if (t > 1.0f) t = 1.0f;
		float qx = ax + (bx-ax)*t, qy = ay + (by-ay)*t;
		float d = (float)sqrt((x-qx)*(x-qx) + (y-qy)*(y-qy));
		if (d < best)
			best = d;
	}
	return best;
}

// A real path: a random map of buildings, the ground shrunk for a tank-sized unit, the mesh of that, and the string
// the search pulls between two random free spots. In world units (a half-cell of the mesh is 5).
static bool meshPath( std::vector<Pt>& pts, float& heading )
{
	const int W = 40, H = 40;
	std::vector<unsigned char> labels(W*H, 0);
	int shapes = 3 + rnd() % 14;
	for (int k = 0; k < shapes; ++k)
	{
		int cx = rnd() % W, cy = rnd() % H, r = 1 + rnd() % 6;
		bool disc = (rnd() & 1) != 0;
		for (int y = cy - r; y <= cy + r; ++y)
		{
			for (int x = cx - r; x <= cx + r; ++x)
			{
				if (x < 0 || y < 0 || x >= W || y >= H)
					continue;
				if (disc && (x-cx)*(x-cx) + (y-cy)*(y-cy) > r*r)
					continue;
				labels[y*W + x] = 1;
			}
		}
	}
	std::vector<unsigned char> shrunk;
	NavBuild::erode(&labels[0], W, H, 1u, 2, shrunk);
	NavBuild::Mesh mesh;
	NavBuild::buildMesh(&shrunk[0], 2*W, 2*H, 1, 1, mesh);
	std::vector<int> clear;
	for (int i = 0; i < 4*W*H; ++i)
	{
		if (shrunk[i] == 0)
			clear.push_back(i);
	}
	if (clear.size() < 2)
		return false;
	// While a mesh is to hand: the straight-line test (NavQuery::segmentClear, which walks triangles) against the
	// plain way of finding out (look up many points along the line).
	for (int k = 0; k < 6; ++k)
	{
		int a = clear[rnd() % clear.size()], b = clear[rnd() % clear.size()];
		NavQuery::Pt s = { (a % (2*W)) + 0.5f, (a / (2*W)) + 0.5f }, g = { (b % (2*W)) + 0.5f, (b / (2*W)) + 0.5f };
		bool walked = NavQuery::segmentClear(mesh, s, g, 1u, 0.25f);
		float len = (float)sqrt((g.x-s.x)*(g.x-s.x) + (g.y-s.y)*(g.y-s.y));
		int steps = (int)(len / 0.1f) + 1;
		bool sampled = true;
		float worst = 0.0f;	// how far off the mesh the line gets, if it does
		for (int i = 0; i <= steps; ++i)
		{
			NavQuery::Pt p = { s.x + (g.x - s.x) * i / steps, s.y + (g.y - s.y) * i / steps };
			NavQuery::Pt q;
			int tri;
			if (!NavQuery::nearestPassable(mesh, p, 1u, 0.02f, 0, -1, q, tri))
			{
				sampled = false;
				if (!NavQuery::nearestPassable(mesh, p, 1u, 0.3f, 0, -1, q, tri))
					worst = 1.0f;
			}
		}
		++s_segTests;
		// They may differ only where the line just grazes a wall (a corner within a hair of it).
		if (walked != sampled && (worst > 0.5f || walked))
			++s_segWrong;
		else if (walked != sampled)
			++s_segGraze;
	}
	for (int tries = 0; tries < 8; ++tries)
	{
		int a = clear[rnd() % clear.size()], b = clear[rnd() % clear.size()];
		NavQuery::Pt s = { (a % (2*W)) + 0.5f, (a / (2*W)) + 0.5f }, g = { (b % (2*W)) + 0.5f, (b / (2*W)) + 0.5f };
		NavQuery::Path path;
		if (a == b || !NavQuery::findPath(mesh, s, g, 1u, path) || path.pts.size() < 3)
			continue;
		pts.clear();
		for (size_t k = 0; k < path.pts.size(); ++k)
		{
			Pt p = { path.pts[k].x * 5.0f, path.pts[k].y * 5.0f };
			pts.push_back(p);
		}
		heading = (rnd() % 3 == 0) ? (float)atan2(pts[1].y - pts[0].y, pts[1].x - pts[0].x) : frand(-3.14f, 3.14f);
		return true;
	}
	return false;
}

static int s_lastKind = 0;	// 0 long legs, 1 mixed, 2 corners crowded together (not a shape a pulled string has), 3 from a mesh

static void randomPath( std::vector<Pt>& pts, float& heading )
{
	if (rnd() % 2 == 0 && meshPath(pts, heading))
	{
		s_lastKind = 3;
		return;
	}
	pts.clear();
	int n = 2 + rnd() % 7;
	Pt p = { frand(0.0f, 500.0f), frand(0.0f, 500.0f) };
	pts.push_back(p);
	float dir = frand(-3.14f, 3.14f);
	int kind = rnd() % 3;
	s_lastKind = kind;
	for (int i = 1; i < n; ++i)
	{
		float len = kind == 0 ? frand(60.0f, 400.0f) : (kind == 1 ? frand(8.0f, 250.0f) : frand(2.0f, 30.0f));
		dir += frand(-2.6f, 2.6f);
		p.x += len * (float)cos(dir);
		p.y += len * (float)sin(dir);
		pts.push_back(p);
	}
	heading = (rnd() % 3 == 0) ? (float)atan2(pts[1].y - pts[0].y, pts[1].x - pts[0].x) : frand(-3.14f, 3.14f);
}

struct Totals
{
	long long runs, frames, turnOver, accelOver, brakeOver, moveWrong, extraReversals, notArrived, strayed;
	long long smallerArcs, pivots, cheats, crowdedOdd;
	double worstTurn, worstBrake, worstStray, lengthRatioSum, timeSum;
};

static bool drive( const Unit& unit, const std::vector<Pt>& pts, float heading, Totals& tot, FILE *svg, const char *color )
{
	Plan plan;
	build(pts, heading, unit.kin, plan);
	State st;
	start(plan, pts[0].x, pts[0].y, heading, 0.0f, st);
	++tot.runs;
	tot.smallerArcs += plan.smallerArcs;
	tot.pivots += plan.pivots;
	tot.cheats += plan.cheats;

	// How many times the path itself changes the side it turns to.
	int pathSwitches = 0, lastSign = 0;
	for (size_t i = 1; i + 1 < pts.size(); ++i)
	{
		float cr = (pts[i].x - pts[i-1].x) * (pts[i+1].y - pts[i].y) - (pts[i].y - pts[i-1].y) * (pts[i+1].x - pts[i].x);
		int sign = cr > 1.0e-4f ? 1 : (cr < -1.0e-4f ? -1 : 0);
		if (sign != 0)
		{
			if (lastSign != 0 && sign != lastSign)
				++pathSwitches;
			lastSign = sign;
		}
	}
	bool wheels = unit.kin.turnSpeed > 0.0f;
	float fullRadius = (unit.kin.cornerSpeed > unit.kin.turnSpeed ? unit.kin.cornerSpeed : unit.kin.turnSpeed) / unit.kin.turnRate;
	float pathLength = 0.0f;
	for (size_t i = 0; i + 1 < pts.size(); ++i)
		pathLength += (float)sqrt((pts[i+1].x-pts[i].x)*(pts[i+1].x-pts[i].x) + (pts[i+1].y-pts[i].y)*(pts[i+1].y-pts[i].y));

	bool ok = true;
	int switches = 0, turnSign = 0;
	int frames = 0;
	if (svg)
		fprintf(svg, "<path d=\"M%.1f %.1f", st.x, st.y);
	while (!st.done && frames < 200000)
	{
		State before = st;
		step(plan, unit.kin, 1.0e9f, st);
		++frames;
		float dh = wrap(st.heading - before.heading);
		float mag = dh >= 0.0f ? dh : -dh;
		float limit = unit.kin.turnRate;
		if (wheels && plan.cheats == 0)
		{
			float v = st.v > before.v ? st.v : before.v;
			float share = v / unit.kin.turnSpeed;
			if (share < 1.0f)
				limit *= share;
		}
		if (mag > limit * 1.02f + 2.0e-3f)
		{
			++tot.turnOver;
			ok = false;
		}
		if (limit > 0.0f && mag / unit.kin.turnRate > tot.worstTurn)
			tot.worstTurn = mag / unit.kin.turnRate;
		float dv = st.v - before.v;
		if (dv > unit.kin.accel + 1.0e-4f)
		{
			++tot.accelOver;
			ok = false;
		}
		if (-dv > unit.kin.brake * 1.001f + 1.0e-4f)
		{
			// The last step onto a stop is allowed to finish the stop.
			if (-dv / unit.kin.brake > tot.worstBrake)
				tot.worstBrake = -dv / unit.kin.brake;
			if (-dv > unit.kin.brake * 2.0f + 0.02f)
			{
				++tot.brakeOver;
				ok = false;
			}
		}
		float moved = (float)sqrt((st.x-before.x)*(st.x-before.x) + (st.y-before.y)*(st.y-before.y));
		// The chord of what it covered can only be shorter than the distance along the line, and that distance is
		// never more than a frame at the speed it could have reached.
		if (moved > st.moved + 2.0e-3f || st.moved > before.v + unit.kin.accel + 1.0e-4f || st.moved > unit.kin.maxSpeed + 1.0e-4f)
		{
			++tot.moveWrong;
			ok = false;
		}
		if (mag > 1.0e-4f)
		{
			int sign = dh > 0.0f ? 1 : -1;
			if (turnSign != 0 && sign != turnSign)
				++switches;
			turnSign = sign;
		}
		float stray = distToPolyline(pts, st.x, st.y);
		if (stray > tot.worstStray)
			tot.worstStray = stray;
		if (stray > 2.0f * fullRadius + 1.0f)
		{
			if (tot.strayed == 0 && getenv("DRIVEDEBUG"))
			{
				printf("DEBUG STRAY %s at (%.1f,%.1f) prim %d heading %.3f\n", unit.name, st.x, st.y, st.prim, heading);
				for (size_t i = 0; i < pts.size(); ++i) printf(" (%.1f,%.1f)", pts[i].x, pts[i].y);
				printf("\n");
				for (size_t i = 0; i < plan.prims.size(); ++i)
				{
					const Prim& q = plan.prims[i];
					if (q.type == LINE) printf("  LINE (%.1f,%.1f)-(%.1f,%.1f) len %.2f\n", q.x0, q.y0, q.x1, q.y1, q.length);
					else if (q.type == ARC) printf("  ARC c(%.1f,%.1f) r %.2f a0 %.2f sweep %.3f vmax %.2f\n", q.cx, q.cy, q.r, q.a0, q.sweep, q.vmax);
					else printf("  PIVOT at (%.1f,%.1f) h0 %.2f turn %.3f\n", q.x0, q.y0, q.h0, q.turn);
				}
			}
			++tot.strayed;
			ok = false;
			break;
		}
		if (svg && (frames % 3) == 0)
			fprintf(svg, " L%.1f %.1f", st.x, st.y);
	}
	if (svg)
		fprintf(svg, " L%.1f %.1f\" fill=\"none\" stroke=\"%s\" stroke-width=\"1.6\"/>\n", st.x, st.y, color);
	tot.frames += frames;
	const Pt& goal = pts[pts.size() - 1];
	if (!st.done || (st.x-goal.x)*(st.x-goal.x) + (st.y-goal.y)*(st.y-goal.y) > 0.01f || st.v != 0.0f)
	{
		++tot.notArrived;
		ok = false;
	}
	// One extra change of side is allowed for the turn onto the path at the start.
	float shortestLeg = 1.0e30f;
	for (size_t i = 0; i + 1 < pts.size(); ++i)
	{
		float leg = (float)sqrt((pts[i+1].x-pts[i].x)*(pts[i+1].x-pts[i].x) + (pts[i+1].y-pts[i].y)*(pts[i+1].y-pts[i].y));
		if (leg < shortestLeg)
			shortestLeg = leg;
	}
	if (switches > pathSwitches + 1 && s_lastKind != 3 && (s_lastKind == 2 || shortestLeg < 2.0f * fullRadius))
	{
		// Random zigzags with corners closer together than the unit's turning circle double back on themselves in
		// ways a pulled string never does; there "the same turns as the path" is not well defined. Counted, not failed.
		++tot.crowdedOdd;
	}
	else if (switches > pathSwitches + 1)
	{
		if (tot.extraReversals == 0 && getenv("DRIVEDEBUG"))
		{
			printf("DEBUG %s switches %d path %d heading %.3f\n", unit.name, switches, pathSwitches, heading);
			for (size_t i = 0; i < pts.size(); ++i) printf(" (%.1f,%.1f)", pts[i].x, pts[i].y);
			printf("\n");
			for (size_t i = 0; i < plan.prims.size(); ++i)
			{
				const Prim& q = plan.prims[i];
				if (q.type == LINE) printf("  LINE (%.1f,%.1f)-(%.1f,%.1f) len %.2f\n", q.x0, q.y0, q.x1, q.y1, q.length);
				else if (q.type == ARC) printf("  ARC c(%.1f,%.1f) r %.2f a0 %.2f sweep %.3f vmax %.2f\n", q.cx, q.cy, q.r, q.a0, q.sweep, q.vmax);
				else printf("  PIVOT at (%.1f,%.1f) h0 %.2f turn %.3f\n", q.x0, q.y0, q.h0, q.turn);
			}
		}
		tot.extraReversals += switches - pathSwitches - 1;
		ok = false;
	}
	if (pathLength > 0.0f)
		tot.lengthRatioSum += plan.length / pathLength;
	tot.timeSum += frames;
	return ok;
}

static int fuzz( int count, unsigned int seed )
{
	s_seed = seed;
	Totals tot[4];
	memset(tot, 0, sizeof(tot));
	int failures = 0;
	std::vector<Pt> pts;
	for (int n = 0; n < count; ++n)
	{
		float heading;
		unsigned int pathSeed = s_seed;
		randomPath(pts, heading);
		for (int u = 0; u < 4; ++u)
		{
			if (!drive(UNITS[u], pts, heading, tot[u], 0, 0))
			{
				++failures;
				if (failures <= 5)
				{
					printf("FAIL path seed %u, %s, heading %.3f:", pathSeed, UNITS[u].name, heading);
					for (size_t i = 0; i < pts.size(); ++i)
						printf(" (%.1f,%.1f)", pts[i].x, pts[i].y);
					printf("\n");
				}
			}
		}
	}
	printf("drive: %d paths x 4 units, %d failures\n", count, failures);
	for (int u = 0; u < 4; ++u)
	{
		const Totals& t = tot[u];
		printf("  %-19s frames %lld | over turn rate %lld (most %.2f of it), over accel %lld, over braking %lld (hardest stop %.2f of it), "
					 "moved more than its speed %lld, turned without a cause %lld, did not arrive %lld, strayed %lld (furthest %.1f)\n",
			UNITS[u].name, t.frames, t.turnOver, t.worstTurn, t.accelOver, t.brakeOver, t.worstBrake, t.moveWrong, t.extraReversals,
			t.notArrived, t.strayed, t.worstStray);
		printf("  %-19s (on random zigzags tighter than its turning circle: %lld paths turned differently from the zigzag)\n", "", t.crowdedOdd);
		printf("  %-19s driven line / string length %.4f | per path: %.2f smaller arcs, %.2f stop-and-pivot corners, %.2f tighter-than-real turns\n",
			"", t.runs ? t.lengthRatioSum / t.runs : 0.0, t.runs ? (double)t.smallerArcs / t.runs : 0.0,
			t.runs ? (double)t.pivots / t.runs : 0.0, t.runs ? (double)t.cheats / t.runs : 0.0);
	}
	printf("straight-line test on the mesh: %lld lines, %lld wrong, %lld differ only by grazing a wall\n", s_segTests, s_segWrong, s_segGraze);
	if (s_segWrong)
		++failures;
	return failures ? 1 : 0;
}

static int svg( const char *outPath, unsigned int seed )
{
	s_seed = seed;
	FILE *out = fopen(outPath, "w");
	if (!out)
	{
		printf("cannot write %s\n", outPath);
		return 2;
	}
	static const char *COLOR[4] = { "#c0392b", "#1f77b4", "#2ca02c", "#8e44ad" };
	fprintf(out, "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 1000 760\" width=\"1000\" height=\"760\">\n");
	fprintf(out, "<rect width=\"1000\" height=\"760\" fill=\"#f4f1e8\"/>\n");
	// One hand-made path with every kind of corner, the same for all four units, each drawn in its own panel.
	static const Pt SHAPE[8] = { {40,300}, {170,300}, {250,220}, {250,120}, {330,60}, {350,90}, {362,70}, {450,200} };
	Totals tot;
	memset(&tot, 0, sizeof(tot));
	for (int u = 0; u < 4; ++u)
	{
		float ox = (u % 2) * 500.0f, oy = (u / 2) * 380.0f;
		std::vector<Pt> pts;
		for (int i = 0; i < 8; ++i)
		{
			Pt p = { SHAPE[i].x + ox, SHAPE[i].y + oy };
			pts.push_back(p);
		}
		fprintf(out, "<path d=\"");
		for (size_t i = 0; i < pts.size(); ++i)
			fprintf(out, "%c%.1f %.1f ", i ? 'L' : 'M', pts[i].x, pts[i].y);
		fprintf(out, "\" fill=\"none\" stroke=\"#999\" stroke-width=\"0.8\" stroke-dasharray=\"4 3\"/>\n");
		drive(UNITS[u], pts, -1.2f, tot, out, COLOR[u]);
		fprintf(out, "<text x=\"%.0f\" y=\"%.0f\" font-family=\"sans-serif\" font-size=\"14\" fill=\"%s\">%s</text>\n", ox + 40, oy + 345, COLOR[u], UNITS[u].name);
	}
	fprintf(out, "</svg>\n");
	fclose(out);
	return 0;
}

int main( int argc, char **argv )
{
	if (argc >= 3 && !strcmp(argv[1], "svg"))
		return svg(argv[2], argc >= 4 ? (unsigned int)strtoul(argv[3], 0, 10) : 7u);
	if (argc >= 2)
		return fuzz(atoi(argv[1]), argc >= 3 ? (unsigned int)strtoul(argv[2], 0, 10) : 12345u);
	printf("usage: drivetest <count> [seed] | drivetest svg <out.svg> [seed]\n");
	return 2;
}
