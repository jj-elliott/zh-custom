// zh-pipeline: standalone test of the navigation mesh builder (Core/GameEngine/.../NavMeshBuild.cpp), no engine.
// Built and run by scripts/navtest.ps1.
//   navtest fuzz <count> [seed]              random label grids, every tolerance, full validation
//   navtest bridges <count> [seed]           random maps with bridges: the atlas search against an exhaustive one
//   navtest svg <grid.txt> <out.svg> <num> <den>   build one grid (as dumped by the engine), draw it, print statistics
// Grid file: "width height" then height lines of width characters, '0'..'9' = label, first line = y 0.

#include "GameLogic/NavMeshBuild.h"
#include "GameLogic/NavMeshQuery.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <queue>
#include <functional>

using namespace NavBuild;

static unsigned int s_seed = 1;
static unsigned int rnd()
{
	s_seed = s_seed * 1664525u + 1013904223u;
	return s_seed >> 8;
}

// For every cell: does a triangle that holds its centre (edges included) have the cell's label? Returns how many
// cells have none.
static int compareWithGrid( const Mesh& mesh, const std::vector<unsigned char>& labels, int *uncovered )
{
	std::vector<unsigned char> state(labels.size(), 0);	// 0 not covered, 1 covered by other labels only, 2 matched
	for (size_t t = 0; t < mesh.tris.size(); ++t)
	{
		const Tri& tr = mesh.tris[t];
		Vert a = mesh.verts[tr.v[0]], b = mesh.verts[tr.v[1]], c = mesh.verts[tr.v[2]];
		int x0 = a.x, x1 = a.x, y0 = a.y, y1 = a.y;
		if (b.x < x0) x0 = b.x; if (b.x > x1) x1 = b.x; if (b.y < y0) y0 = b.y; if (b.y > y1) y1 = b.y;
		if (c.x < x0) x0 = c.x; if (c.x > x1) x1 = c.x; if (c.y < y0) y0 = c.y; if (c.y > y1) y1 = c.y;
		Vert a2 = { a.x*2, a.y*2 }, b2 = { b.x*2, b.y*2 }, c2 = { c.x*2, c.y*2 };
		for (int y = y0; y < y1; ++y)
		{
			for (int x = x0; x < x1; ++x)
			{
				Vert p = { 2*x + 1, 2*y + 1 };
				if (orient(a2, b2, p) >= 0 && orient(b2, c2, p) >= 0 && orient(c2, a2, p) >= 0)
				{
					unsigned char& st = state[y*mesh.width + x];
					if (tr.label == labels[y*mesh.width + x])
						st = 2;
					else if (st == 0)
						st = 1;
				}
			}
		}
	}
	int differ = 0;
	*uncovered = 0;
	for (size_t i = 0; i < labels.size(); ++i)
	{
		if (state[i] == 0)
			++*uncovered;
		else if (state[i] == 1)
			++differ;
	}
	return differ;
}

static void randomGrid( std::vector<unsigned char>& labels, int& w, int& h )
{
	w = 3 + rnd() % 46;
	h = 3 + rnd() % 46;
	labels.assign(w*h, 0);
	int kind = rnd() % 4;
	int numLabels = 2 + rnd() % 3;
	if (kind == 0)
	{
		// noise
		int density = 1 + rnd() % 60;
		for (int i = 0; i < w*h; ++i)
		{
			if ((int)(rnd() % 100) < density)
				labels[i] = (unsigned char)(1 + rnd() % (numLabels - 1));
		}
	}
	else if (kind == 1)
	{
		// rectangles and discs
		int shapes = 1 + rnd() % 12;
		for (int s = 0; s < shapes; ++s)
		{
			int cx = rnd() % w, cy = rnd() % h, r = 1 + rnd() % 9;
			unsigned char label = (unsigned char)(rnd() % numLabels);
			bool disc = (rnd() & 1) != 0;
			for (int y = cy - r; y <= cy + r; ++y)
			{
				for (int x = cx - r; x <= cx + r; ++x)
				{
					if (x < 0 || y < 0 || x >= w || y >= h)
						continue;
					if (disc && (x-cx)*(x-cx) + (y-cy)*(y-cy) > r*r)
						continue;
					labels[y*w + x] = label;
				}
			}
		}
	}
	else if (kind == 2)
	{
		// diagonal bands (staircases)
		int a = 1 + rnd() % 5, b = 1 + rnd() % 5, period = 4 + rnd() % 20;
		for (int y = 0; y < h; ++y)
		{
			for (int x = 0; x < w; ++x)
				labels[y*w + x] = (unsigned char)((((a*x + b*y) / period) % numLabels));
		}
	}
	else
	{
		// a winding one-cell wall with gaps, plus noise
		for (int y = 0; y < h; ++y)
		{
			for (int x = 0; x < w; ++x)
			{
				if ((x % 6 == 3 && (y + x/6*5) % 11 != 0) || (int)(rnd() % 100) < 3)
					labels[y*w + x] = 1;
			}
		}
	}
}

//-------------------------------------------------------------------------------------------------
// Paths: the mesh search against a plain search on the grid the mesh was built from (tolerance 0, so they describe
// the same ground).
struct PathTotals
{
	long long pairs, found, existMismatch, offGround, longer, expanded, corridors, tooClose;
	double ratioSum, worstRatio, worstClearance;
};
static PathTotals s_paths[6];	// unit radius 0, 0.5 and 1.5 cells; exact boundaries, then simplified

// Octile distances on the grid from one cell; a diagonal step needs both cells beside it free, so every grid path is
// also a path a point could walk. -1 = unreachable.
static void gridDistances( const std::vector<unsigned char>& labels, int w, int h, int from, std::vector<float>& distOut )
{
	distOut.assign(w*h, -1.0f);
	typedef std::pair<float, int> Item;
	std::priority_queue<Item, std::vector<Item>, std::greater<Item> > open;
	distOut[from] = 0.0f;
	open.push(Item(0.0f, from));
	while (!open.empty())
	{
		Item it = open.top();
		open.pop();
		if (it.first > distOut[it.second])
			continue;
		int x = it.second % w, y = it.second / w;
		for (int dy = -1; dy <= 1; ++dy)
		{
			for (int dx = -1; dx <= 1; ++dx)
			{
				if (!dx && !dy)
					continue;
				int nx = x + dx, ny = y + dy;
				if (nx < 0 || ny < 0 || nx >= w || ny >= h || labels[ny*w + nx] != 0)
					continue;
				if (dx && dy && (labels[y*w + nx] != 0 || labels[ny*w + x] != 0))
					continue;
				float d = it.first + ((dx && dy) ? 1.41421356f : 1.0f);
				if (distOut[ny*w + nx] < 0.0f || d < distOut[ny*w + nx])
				{
					distOut[ny*w + nx] = d;
					open.push(Item(d, ny*w + nx));
				}
			}
		}
	}
}

// Distance from p to the nearest cell that is not clear (or to the map edge), looking no further than 'reach'.
static float clearanceAt( const std::vector<unsigned char>& labels, int w, int h, float px, float py, float reach )
{
	float best = reach;
	if (px < best) best = px;
	if (py < best) best = py;
	if (w - px < best) best = w - px;
	if (h - py < best) best = h - py;
	int x0 = (int)floor(px - reach), x1 = (int)floor(px + reach);
	int y0 = (int)floor(py - reach), y1 = (int)floor(py + reach);
	for (int y = y0; y <= y1; ++y)
	{
		for (int x = x0; x <= x1; ++x)
		{
			if (x < 0 || y < 0 || x >= w || y >= h || labels[y*w + x] == 0)
				continue;
			float cx = px < x ? (float)x : (px > x + 1 ? (float)(x + 1) : px);
			float cy = py < y ? (float)y : (py > y + 1 ? (float)(y + 1) : py);
			float d = (float)sqrt((px-cx)*(px-cx) + (py-cy)*(py-cy));
			if (d < best)
				best = d;
		}
	}
	return best;
}

// The mesh search against a plain search on the grid the mesh was built from (tolerance 0, so they describe the
// same ground). For a mesh of eroded ground (scale 2: half-cells), every point of every path must also be at least
// the unit's radius from every wall of the original grid.
static void pathFuzz( PathTotals& tot, const Mesh& mesh, const std::vector<unsigned char>& labels, int w, int h, bool *failed,
											const std::vector<unsigned char>& original, int ow, int oh, float scale, float radiusCells, float slackCells )
{
	std::vector<int> clear;
	for (int i = 0; i < w*h; ++i)
	{
		if (labels[i] == 0)
			clear.push_back(i);
	}
	if (clear.size() < 2)
		return;
	std::vector<float> gridDist;
	for (int n = 0; n < 6; ++n)
	{
		int a = clear[rnd() % clear.size()], b = clear[rnd() % clear.size()];
		if (a == b)
			continue;
		gridDistances(labels, w, h, a, gridDist);
		NavQuery::Pt s = { (a % w) + 0.5f, (a / w) + 0.5f }, g = { (b % w) + 0.5f, (b / w) + 0.5f };
		NavQuery::Path path;
		bool found = NavQuery::findPath(mesh, s, g, 1u, path);
		++tot.pairs;
		tot.expanded += path.expanded;
		tot.corridors += path.corridors;
		// Neither search lets a point slip between two cells that touch only at a corner, so they must agree exactly.
		if (found != (gridDist[b] >= 0.0f))
		{
			++tot.existMismatch;
			*failed = true;
			continue;
		}
		if (!found)
			continue;
		++tot.found;
		bool off = false, close = false;
		for (size_t k = 0; k + 1 < path.pts.size() && !off; ++k)
		{
			float len = (float)sqrt((path.pts[k+1].x-path.pts[k].x)*(path.pts[k+1].x-path.pts[k].x) + (path.pts[k+1].y-path.pts[k].y)*(path.pts[k+1].y-path.pts[k].y));
			int steps = (int)(len / 0.05f) + 1;
			for (int i = 0; i <= steps; ++i)
			{
				NavQuery::Pt p = { path.pts[k].x + (path.pts[k+1].x - path.pts[k].x) * i / steps, path.pts[k].y + (path.pts[k+1].y - path.pts[k].y) * i / steps };
				int t = NavQuery::locate(mesh, p, 1u);
				if (t < 0 || mesh.tris[t].label != 0)
				{
					off = true;
					break;
				}
				if (radiusCells > 0.0f)
				{
					double c = clearanceAt(original, ow, oh, p.x / scale, p.y / scale, radiusCells + 1.0f) / radiusCells;
					if (c < tot.worstClearance)
						tot.worstClearance = c;
					if (c * radiusCells < radiusCells - slackCells - 0.001)
						close = true;
				}
			}
		}
		if (off) { ++tot.offGround; *failed = true; }
		if (close) { ++tot.tooClose; *failed = true; }
		double ratio = path.length / gridDist[b];
		tot.ratioSum += ratio;
		if (ratio > tot.worstRatio)
			tot.worstRatio = ratio;
		if (path.length > gridDist[b] * 1.0005f + 0.001f)
			++tot.longer;
	}
}

static int fuzz( int count, unsigned int seed )
{
	for (int q = 0; q < 6; ++q)
		s_paths[q].worstClearance = 99.0;
	s_seed = seed;
	static const int TOL[4][2] = { {0,1}, {1,2}, {1,1}, {3,2} };
	int failures = 0;
	long long totalCells = 0, totalDiffer[4] = {0,0,0,0}, totalTris[4] = {0,0,0,0}, reverted[4] = {0,0,0,0};
	std::vector<unsigned char> labels;
	std::vector<char> report;
	for (int n = 0; n < count; ++n)
	{
		int w, h;
		unsigned int gridSeed = s_seed;
		randomGrid(labels, w, h);
		totalCells += w*h;
		for (int k = 0; k < 4; ++k)
		{
			Mesh mesh;
			buildMesh(&labels[0], w, h, TOL[k][0], TOL[k][1], mesh);
			int bad = validate(mesh, report);
			int uncovered = 0;
			int differ = compareWithGrid(mesh, labels, &uncovered);
			if (uncovered)
				++bad;
			if (differ)
				++bad;	// at any tolerance, no cell centre may change side.
			if ((k == 0 || k == 2) && !bad)
			{
				// Simplified boundaries may give back a sliver of the shrunk ground: under 0.36 of a cell (a half-cell
				// stair step's diagonal).
				PathTotals *tot = &s_paths[k == 0 ? 0 : 3];
				float slack = k == 0 ? 0.0f : 0.36f;
				bool pathFailed = false;
				pathFuzz(tot[0], mesh, labels, w, h, &pathFailed, labels, w, h, 1.0f, 0.0f, 0.0f);
				// Wide units: ground shrunk by the radius, in half-cells, then the same search as a point.
				static const int RADIUS_HALF_CELLS[2] = { 1, 3 };
				for (int r = 0; r < 2; ++r)
				{
					std::vector<unsigned char> shrunk;
					erode(&labels[0], w, h, 1u, RADIUS_HALF_CELLS[r], shrunk);
					Mesh wide;
					buildMesh(&shrunk[0], 2*w, 2*h, TOL[k][0], TOL[k][1], wide);
					std::vector<char> wideReport;
					if (validate(wide, wideReport))
						pathFailed = true;
					pathFuzz(tot[1 + r], wide, shrunk, 2*w, 2*h, &pathFailed, labels, w, h, 2.0f, 0.5f * RADIUS_HALF_CELLS[r], slack);
				}
				if (pathFailed)
				{
					++bad;
					report.clear();
					const char *msg = "path: the mesh search disagrees with the grid search, or left clear ground\n";
					report.insert(report.end(), msg, msg + strlen(msg) + 1);
				}
			}
			totalDiffer[k] += differ;
			totalTris[k] += (long long)mesh.tris.size();
			reverted[k] += mesh.stats.revertedChains;
			if (bad)
			{
				++failures;
				if (failures <= 5)
				{
					printf("FAIL grid seed %u (%dx%d) tol %d/%d: %d violations, %d cells differ, %d uncovered\n%s",
						gridSeed, w, h, TOL[k][0], TOL[k][1], bad, differ, uncovered, &report[0]);
					for (int y = h - 1; y >= 0 && h <= 24; --y)
					{
						for (int x = 0; x < w; ++x)
							putchar('0' + labels[y*w + x]);
						putchar('\n');
					}
				}
			}
		}
	}
	printf("fuzz: %d grids, %lld cells, %d failures\n", count, totalCells, failures);
	for (int k = 0; k < 4; ++k)
		printf("  tol %d/%d: %lld triangles, %lld cells on the wrong side of a boundary, %lld chains reverted\n",
			TOL[k][0], TOL[k][1], totalTris[k], totalDiffer[k], reverted[k]);
	static const char *NAME[6] = { "exact, radius 0  ", "exact, radius 0.5", "exact, radius 1.5", "tol 1, radius 0  ", "tol 1, radius 0.5", "tol 1, radius 1.5" };
	for (int r = 0; r < 6; ++r)
	{
		const PathTotals& t = s_paths[r];
		printf("paths, %s: %lld pairs, %lld with a path; existence differs from the grid %lld, left the mesh %lld, nearer a wall than the radius %lld (nearest %.3f radii)\n",
			NAME[r], t.pairs, t.found, t.existMismatch, t.offGround, t.tooClose, (r % 3) ? t.worstClearance : 0.0);
		printf("    length / grid length: mean %.3f, worst %.3f, longer than the grid path %lld; %.1f nodes and %.2f corridors a search\n",
			t.found ? t.ratioSum / t.found : 0.0, t.worstRatio, t.longer,
			t.pairs ? (double)t.expanded / t.pairs : 0.0, t.pairs ? (double)t.corridors / t.pairs : 0.0);
	}
	return failures ? 1 : 0;
}

//-------------------------------------------------------------------------------------------------
// Bridges: random maps with one to three bridges, each a second level over its footprint (NavMeshQuery.h, Atlas).
// The atlas search against an exhaustive search over half-cells that knows nothing of sheets or of which sheet has
// which ground: a unit's centre may be on any half-cell of ground that some sheet allows, or on a bridge's deck,
// and it gets on and off a deck where the bridge's own sheet has ground beside it.
struct BridgeTotals
{
	long long worlds, pairs, found, refFound, phantom, missed, offGround, tooClose, wrongDeck, badSwitch, longer, viaBridge;
	long long expanded, links, sheets, tris, invalidMesh, fallbacks;
	double ratioSum, worstRatio, worstClearance;
};
static BridgeTotals s_bridge[6];	// radius 0 / 0.5 / 1.5 cells; exact, then simplified
// A half-cell is ground if its centre is far enough from every wall, so a corner of it can be nearer: by 0.086 of a
// cell at most for the radii tried (1.5 cells: 0.943 radii). Round a wall such a corner is a dent in the ground that
// no pulled string enters, which is why the single-mesh fuzz allows nothing. Where two sheets' ground meets, the
// dent can be the doorway between them, and a string through it runs along that half-cell's edge.
static const float SHEET_SLACK = 0.09f;
static int s_verbose = 0;	// "bridgeworld": print every pair that fails or is missed

struct FuzzBridge
{
	std::vector<unsigned char> view;	// whole map: what a unit on this bridge may stand on (0) or not (1)
	std::vector<unsigned char> deck;	// whole map: 1 = the bridge itself
	int x0, y0, x1, y1;								// cells the bridge touches: [x0, x1) x [y0, y1)
};

static void randomBridgeWorld( std::vector<unsigned char>& ground, int& w, int& h, std::vector<FuzzBridge>& bridges )
{
	w = 16 + rnd() % 33;
	h = 16 + rnd() % 33;
	ground.assign(w*h, 0);
	int kind = rnd() % 3;
	if (kind == 0)
	{
		int density = 1 + rnd() % 12;
		for (int i = 0; i < w*h; ++i)
		{
			if ((int)(rnd() % 100) < density)
				ground[i] = 1;
		}
	}
	else if (kind == 1)
	{
		int shapes = 1 + rnd() % 8;
		for (int s = 0; s < shapes; ++s)
		{
			int cx = rnd() % w, cy = rnd() % h, r = 1 + rnd() % 6;
			bool disc = (rnd() & 1) != 0;
			for (int y = cy - r; y <= cy + r; ++y)
			{
				for (int x = cx - r; x <= cx + r; ++x)
				{
					if (x < 0 || y < 0 || x >= w || y >= h || (disc && (x-cx)*(x-cx) + (y-cy)*(y-cy) > r*r))
						continue;
					ground[y*w + x] = 1;
				}
			}
		}
	}
	else
	{
		// a river: a band of wall right across the map, so that the bridges are the only way over
		bool vertical = (rnd() & 1) != 0;
		int width = 3 + rnd() % 8;
		int room = (vertical ? w : h) - width - 7;
		int at = 4 + rnd() % (room < 1 ? 1 : room);
		int wobble = rnd() % 3;
		for (int y = 0; y < h; ++y)
		{
			for (int x = 0; x < w; ++x)
			{
				int along = vertical ? y : x, across = vertical ? x : y;
				int shift = wobble ? ((along / 5) % (wobble + 1)) : 0;
				if (across >= at + shift && across < at + shift + width)
					ground[y*w + x] = 1;
			}
		}
	}

	static const int DIRS[8][2] = { {1,0}, {0,1}, {1,1}, {1,-1}, {2,1}, {1,2}, {2,-1}, {1,-2} };
	int count = 1 + rnd() % 3;
	bridges.clear();
	std::vector<unsigned char> block(w*h, 0), open(w*h, 0), taken(w*h, 0);
	for (int b = 0; b < count; ++b)
	{
		FuzzBridge br;
		int d = rnd() % 8;
		float len = (float)sqrt((double)(DIRS[d][0]*DIRS[d][0] + DIRS[d][1]*DIRS[d][1]));
		float ux = DIRS[d][0] / len, uy = DIRS[d][1] / len;
		float cx = 3.0f + (float)(rnd() % (w - 6)) + 0.5f * (rnd() & 1), cy = 3.0f + (float)(rnd() % (h - 6)) + 0.5f * (rnd() & 1);
		float hl = 4.0f + (float)(rnd() % 21) * 0.5f, hw = 1.5f + (float)(rnd() % 6) * 0.5f;
		int clearance = rnd() % 3;	// 0 low: no ground under it; 1 high; 2 low near its ends
		bool openMouths = (rnd() & 1) != 0;
		br.view.assign(w*h, 0);
		br.deck.assign(w*h, 0);
		br.x0 = w; br.y0 = h; br.x1 = 0; br.y1 = 0;
		std::vector<unsigned char> myBlock(w*h, 0), myOpen(w*h, 0);
		bool clash = false;
		for (int y = 0; y < h; ++y)
		{
			for (int x = 0; x < w; ++x)
			{
				int inside = 0;
				for (int c = 0; c < 4; ++c)
				{
					float px = (float)(x + (c & 1)) - cx, py = (float)(y + (c >> 1)) - cy;
					float along = px*ux + py*uy, lat = -px*uy + py*ux;
					if (fabs(along) <= hl && fabs(lat) <= hw)
						++inside;
				}
				float px = (float)x + 0.5f - cx, py = (float)y + 0.5f - cy;
				float along = (float)fabs(px*ux + py*uy), lat = (float)fabs(-px*uy + py*ux);
				bool mouth = lat < hw - 1.0f && along > hl - 1.0f && along < hl + 1.0f;
				int what = 0;	// 1 deck, 2 side
				if (inside == 4 || mouth)
					what = 1;
				else if (inside > 0)
					what = 2;
				if (!what)
				{
					if (openMouths && lat < hw + 2.0f && along >= hl + 1.0f && along < hl + 4.0f)
						myOpen[y*w + x] = 1;
					continue;
				}
				// Bridges do not touch one another: three cells of other things between any two.
				for (int ny = y - 3; ny <= y + 3 && !clash; ++ny)
				{
					for (int nx = x - 3; nx <= x + 3; ++nx)
					{
						if (nx >= 0 && ny >= 0 && nx < w && ny < h && taken[ny*w + nx])
						{
							clash = true;
							break;
						}
					}
				}
				br.deck[y*w + x] = (unsigned char)(what == 1 ? 1 : 0);
				br.view[y*w + x] = (unsigned char)what;
				if (x < br.x0) br.x0 = x;
				if (y < br.y0) br.y0 = y;
				if (x + 1 > br.x1) br.x1 = x + 1;
				if (y + 1 > br.y1) br.y1 = y + 1;
				if (inside > 0 && !mouth && (clearance == 0 || (clearance == 2 && along > hl - 4.0f)))
					myBlock[y*w + x] = 1;
			}
		}
		if (clash || br.x1 <= br.x0)
			continue;
		for (int i = 0; i < w*h; ++i)
		{
			if (br.view[i]) taken[i] = 1;
			if (myBlock[i]) block[i] = 1;
			if (myOpen[i]) open[i] = 1;
		}
		bridges.push_back(br);
	}
	int i;
	for (i = 0; i < w*h; ++i)
	{
		if (open[i] && !taken[i]) ground[i] = 0;
		if (block[i]) ground[i] = 1;
	}
	for (size_t b = 0; b < bridges.size(); ++b)
	{
		FuzzBridge& br = bridges[b];
		for (i = 0; i < w*h; ++i)
			br.view[i] = (unsigned char)(br.view[i] == 1 ? 0 : (br.view[i] == 2 ? 1 : ground[i]));
	}
}

struct FuzzSheet
{
	std::vector<unsigned char> seen;	// half-cells as this sheet's unit sees them, before the ground is shared out
	int ox, oy, w, h;									// of 'seen', half-cells
	Mesh mesh;
	int mox, moy;											// the mesh's origin
	const std::vector<unsigned char> *view;	// whole map, cells: the walls this sheet's unit keeps its radius from
	unsigned char at( int gx, int gy ) const
	{
		int x = gx - ox, y = gy - oy;
		return (x < 0 || y < 0 || x >= w || y >= h) ? (unsigned char)SHEET_WALL : seen[y*w + x];
	}
};

static void bridgeFuzzWorld( BridgeTotals& tot, const std::vector<unsigned char>& ground, int w, int h,
														 const std::vector<FuzzBridge>& bridges, int radius, int num, int den, float slackCells, bool *failed )
{
	std::vector<FuzzSheet> sheets(bridges.size() + 1);
	std::vector<SheetCells> cells(bridges.size() + 1);
	std::vector<char> report;
	erode(&ground[0], w, h, 1u, radius, cells[0].cells);
	cells[0].ox = cells[0].oy = 0;
	cells[0].w = 2*w;
	cells[0].h = 2*h;
	sheets[0].view = &ground;
	int need = (radius + 1) / 2;	// cells of surroundings that can shrink the window's ground
	int margin = need + 1;				// cells of ground kept round the bridge
	size_t b;
	for (b = 0; b < bridges.size(); ++b)
	{
		const FuzzBridge& br = bridges[b];
		int x0 = br.x0 - margin, y0 = br.y0 - margin, x1 = br.x1 + margin, y1 = br.y1 + margin;
		if (x0 < 0) x0 = 0;
		if (y0 < 0) y0 = 0;
		if (x1 > w) x1 = w;
		if (y1 > h) y1 = h;
		int px0 = x0 - need, py0 = y0 - need, px1 = x1 + need, py1 = y1 + need;
		if (px0 < 0) px0 = 0;
		if (py0 < 0) py0 = 0;
		if (px1 > w) px1 = w;
		if (py1 > h) py1 = h;
		int pw = px1 - px0, ph = py1 - py0;
		std::vector<unsigned char> labels(pw*ph), deck(pw*ph);
		for (int y = 0; y < ph; ++y)
		{
			for (int x = 0; x < pw; ++x)
			{
				labels[y*pw + x] = br.view[(y + py0)*w + (x + px0)];
				deck[y*pw + x] = br.deck[(y + py0)*w + (x + px0)];
			}
		}
		int pad[4] = { x0 - px0, y0 - py0, px1 - x1, py1 - y1 };
		unsigned mapEdges = (px0 == 0 ? 1u : 0u) | (py0 == 0 ? 2u : 0u) | (px1 == w ? 4u : 0u) | (py1 == h ? 8u : 0u);
		erodeSheet(&labels[0], &deck[0], pw, ph, 1u, radius, pad, mapEdges, cells[b+1].cells);
		cells[b+1].ox = 2*x0;
		cells[b+1].oy = 2*y0;
		cells[b+1].w = 2*(x1 - x0);
		cells[b+1].h = 2*(y1 - y0);
		sheets[b+1].view = &br.view;
	}
	size_t s;
	for (s = 0; s < sheets.size(); ++s)
	{
		sheets[s].seen = cells[s].cells;
		sheets[s].ox = cells[s].ox;
		sheets[s].oy = cells[s].oy;
		sheets[s].w = cells[s].w;
		sheets[s].h = cells[s].h;
	}
	assignSheets(cells);
	NavQuery::Atlas atlas;
	for (s = 0; s < sheets.size(); ++s)
	{
		buildMesh(&cells[s].cells[0], cells[s].w, cells[s].h, num, den, sheets[s].mesh, SHEET_EXACT);
		sheets[s].mox = cells[s].ox;
		sheets[s].moy = cells[s].oy;
		if (validate(sheets[s].mesh, report))
		{
			++tot.invalidMesh;
			*failed = true;
			return;
		}
		atlas.addSheet(&sheets[s].mesh, cells[s].ox, cells[s].oy);
		tot.tris += (long long)sheets[s].mesh.tris.size();
	}
	atlas.connect();
	++tot.worlds;
	tot.links += (long long)atlas.links.size();
	tot.sheets += (long long)sheets.size();

	// The exhaustive search's nodes: the ground (one layer: wherever any sheet has ground), then each bridge's deck.
	const int W = 2*w, H = 2*h;
	std::vector<unsigned char> anyGround(W*H, 0);
	std::vector<int> standable;
	for (int i = 0; i < W*H; ++i)
	{
		for (s = 0; s < sheets.size(); ++s)
		{
			if (sheets[s].at(i % W, i / W) == SHEET_GROUND)
				anyGround[i] = 1;
		}
		if (anyGround[i])
			standable.push_back(i);
	}
	for (s = 1; s < sheets.size(); ++s)
	{
		for (int i = 0; i < W*H; ++i)
		{
			if (sheets[s].at(i % W, i / W) == SHEET_DECK)
				standable.push_back((int)s*W*H + i);
		}
	}
	if (standable.size() < 2)
		return;
	std::vector<float> distTo(sheets.size() * W * H);
	typedef std::pair<float, int> Item;
	for (int n = 0; n < 8; ++n)
	{
		int from = standable[rnd() % standable.size()];
		// Half of the goals on a deck, so that bridges get crossed.
		int to = standable[rnd() % standable.size()];
		if ((rnd() & 1) && standable.back() >= W*H)
		{
			for (int tries = 0; tries < 20 && to < W*H; ++tries)
				to = standable[rnd() % standable.size()];
		}
		if (from == to)
			continue;
		int fs = from / (W*H), ts = to / (W*H);	// 0 = on the ground, else on that bridge's deck
		int fgx = (from % (W*H)) % W, fgy = (from % (W*H)) / W;
		int tgx = (to % (W*H)) % W, tgy = (to % (W*H)) / W;

		// Exhaustive: 8 directions (a diagonal step needs both half-cells beside it), on the ground, on a deck, and
		// between a deck and the ground its own sheet has beside it.
		std::fill(distTo.begin(), distTo.end(), -1.0f);
		std::priority_queue<Item, std::vector<Item>, std::greater<Item> > open;
		distTo[from] = 0.0f;
		open.push(Item(0.0f, from));
		while (!open.empty())
		{
			Item it = open.top();
			open.pop();
			if (it.first > distTo[it.second])
				continue;
			int layer = it.second / (W*H);
			int x = (it.second % (W*H)) % W, y = (it.second % (W*H)) / W;
			for (int dy = -1; dy <= 1; ++dy)
			{
				for (int dx = -1; dx <= 1; ++dx)
				{
					if (!dx && !dy)
						continue;
					int nx = x + dx, ny = y + dy;
					if (nx < 0 || ny < 0 || nx >= W || ny >= H)
						continue;
					float d = it.first + ((dx && dy) ? 1.41421356f : 1.0f);
					// Each of: stay on this layer; from the ground up onto each deck; from a deck down to the ground.
					for (size_t target = 0; target < sheets.size(); ++target)
					{
						bool ok;
						if (layer == 0 && target == 0)
						{
							ok = anyGround[ny*W + nx] != 0;
							if (ok && dx && dy)
								ok = anyGround[y*W + nx] && anyGround[ny*W + x];
						}
						else
						{
							// Everything here happens on one bridge's sheet, as that sheet sees it.
							size_t bs = layer ? (size_t)layer : target;
							if (layer != 0 && target != 0 && target != (size_t)layer)
								continue;
							const FuzzSheet& sh = sheets[bs];
							unsigned char here = sh.at(x, y), there = sh.at(nx, ny);
							unsigned char wantHere = (unsigned char)(layer ? SHEET_DECK : SHEET_GROUND);
							unsigned char wantThere = (unsigned char)(target ? SHEET_DECK : SHEET_GROUND);
							ok = (here == wantHere && there == wantThere);
							if (ok && dx && dy)
								ok = sh.at(nx, y) != SHEET_WALL && sh.at(x, ny) != SHEET_WALL;
						}
						if (!ok)
							continue;
						int node = (int)target*W*H + ny*W + nx;
						if (distTo[node] < 0.0f || d < distTo[node])
						{
							distTo[node] = d;
							open.push(Item(d, node));
						}
					}
				}
			}
		}
		float refDist = distTo[to];

		NavQuery::Pt sp = { fgx + 0.5f, fgy + 0.5f }, gp = { tgx + 0.5f, tgy + 0.5f };
		NavQuery::AtlasPath path;
		int result = NavQuery::findPath(atlas, sp, fs, fs == 0, gp, ts, ts == 0, 0.05f, path);
		bool found = (result == NavQuery::PATH_FOUND);
		++tot.pairs;
		tot.expanded += path.expanded;
		tot.fallbacks += path.fallback;
		if (refDist >= 0.0f)
			++tot.refFound;
		if (found != (refDist >= 0.0f))
		{
			if (found) ++tot.phantom; else ++tot.missed;
			*failed = true;
			if (s_verbose)
				printf("%s: (%.1f,%.1f) %s %d -> (%.1f,%.1f) %s %d: result %d, exhaustive %.1f\n", found ? "PHANTOM" : "MISSED", sp.x, sp.y,
					fs ? "deck" : "ground", fs, gp.x, gp.y, ts ? "deck" : "ground", ts, result, refDist);
			continue;
		}
		if (!found)
			continue;
		++tot.found;
		bool off = false, close = false, wrongDeck = false, badSwitch = false, usedBridge = false;
		float radiusCells = 0.5f * radius;
		for (size_t k = 0; k + 1 < path.pts.size(); ++k)
		{
			const FuzzSheet& sh = sheets[path.sheet[k]];
			NavQuery::Pt a = { path.pts[k].x - sh.mox, path.pts[k].y - sh.moy }, c = { path.pts[k+1].x - sh.mox, path.pts[k+1].y - sh.moy };
			if (path.deck[k])
				usedBridge = true;
			if (k > 0 && path.sheet[k] != path.sheet[k-1])
			{
				// The same spot must be ground on the sheet left and on the sheet entered.
				const FuzzSheet& was = sheets[path.sheet[k-1]];
				NavQuery::Pt q = { path.pts[k].x - was.mox, path.pts[k].y - was.moy };
				int t1 = NavQuery::locate(was.mesh, q, 1u), t2 = NavQuery::locate(sh.mesh, a, 1u);
				if (t1 < 0 || t2 < 0 || was.mesh.tris[t1].label != SHEET_GROUND || sh.mesh.tris[t2].label != SHEET_GROUND)
					badSwitch = true;
			}
			float len = (float)sqrt((c.x-a.x)*(c.x-a.x) + (c.y-a.y)*(c.y-a.y));
			int steps = (int)(len / 0.05f) + 1;
			for (int i = 0; i <= steps; ++i)
			{
				NavQuery::Pt p = { a.x + (c.x - a.x) * i / steps, a.y + (c.y - a.y) * i / steps };
				int t = NavQuery::locate(sh.mesh, p, 5u);
				if (t < 0 || (sh.mesh.tris[t].label != SHEET_GROUND && sh.mesh.tris[t].label != SHEET_DECK))
				{
					off = true;
					break;
				}
				// On the bridge exactly where the path says so (not judged within a step of where it changes).
				if (i > 0 && i < steps && len > 0.2f)
				{
					int want = path.deck[k] ? SHEET_DECK : SHEET_GROUND;
					int tw = NavQuery::locate(sh.mesh, p, 1u << want);
					if (tw < 0 || sh.mesh.tris[tw].label != want)
						wrongDeck = true;
				}
				if (radiusCells > 0.0f)
				{
					// On a deck: clear of that bridge's walls. On the ground: clear of the walls as the ground sheet has
					// them, or as a bridge has them whose window the point is in (a unit's centre may be wherever any sheet
					// says it may; on the line between two sheets' ground the path is on both).
					float gx = p.x + sh.mox, gy = p.y + sh.moy;
					double cl = clearanceAt(*sh.view, w, h, gx / 2.0f, gy / 2.0f, radiusCells + 1.0f) / radiusCells;
					for (size_t o = 0; o < sheets.size() && !path.deck[k]; ++o)
					{
						const FuzzSheet& other = sheets[o];
						if (gx < other.ox || gy < other.oy || gx > other.ox + other.w || gy > other.oy + other.h)
							continue;
						double c2 = clearanceAt(*other.view, w, h, gx / 2.0f, gy / 2.0f, radiusCells + 1.0f) / radiusCells;
						if (c2 > cl)
							cl = c2;
					}
					if (cl < tot.worstClearance)
						tot.worstClearance = cl;
					if (cl * radiusCells < radiusCells - slackCells - 0.001)
						close = true;
				}
			}
			if (off)
				break;
		}
		if (off) { ++tot.offGround; *failed = true; }
		if (close) { ++tot.tooClose; *failed = true; }
		if (wrongDeck) { ++tot.wrongDeck; *failed = true; }
		if (badSwitch) { ++tot.badSwitch; *failed = true; }
		if (usedBridge) ++tot.viaBridge;
		bool longer = refDist > 0.0f && path.length > refDist * 1.0005f + 0.001f;
		if (refDist > 0.0f)
		{
			double ratio = path.length / refDist;
			tot.ratioSum += ratio;
			if (ratio > tot.worstRatio)
				tot.worstRatio = ratio;
			if (longer)
				++tot.longer;
		}
		if (s_verbose && (off || close || wrongDeck || badSwitch || (s_verbose > 1 && longer)))
		{
			printf("%s%s%s%s%s: (%.1f,%.1f) %s %d -> (%.1f,%.1f) %s %d: length %.2f, exhaustive %.2f\n", off ? "OFF " : "", close ? "CLOSE " : "",
				wrongDeck ? "DECK " : "", badSwitch ? "SWITCH " : "", longer ? "LONGER " : "", sp.x, sp.y, fs ? "deck" : "ground", fs,
				gp.x, gp.y, ts ? "deck" : "ground", ts, path.length, refDist);
			for (size_t k = 0; k < path.pts.size(); ++k)
				printf("    (%.3f, %.3f) sheet %d deck %d inserted %d\n", path.pts[k].x, path.pts[k].y, path.sheet[k], path.deck[k], path.inserted[k]);
		}
	}
}

// One world of the bridge fuzz, in detail: navtest bridgeworld <world seed> <radius half-cells> <0 exact | 1 simplified> [2 = also longer paths]
static int bridgeWorld( unsigned int seed, int radius, int simplified, int verbose )
{
	s_seed = seed;
	std::vector<unsigned char> ground;
	std::vector<FuzzBridge> bridges;
	int w, h;
	randomBridgeWorld(ground, w, h, bridges);
	// The same pairs as the fuzz picks: it runs the six builds one after another on one random stream.
	static const int RADIUS[3] = { 0, 1, 3 };
	bool failed = false;
	for (int k = 0; k < 6; ++k)
	{
		BridgeTotals tot;
		memset(&tot, 0, sizeof(tot));
		tot.worstClearance = 99.0;
		bool mine = (RADIUS[k % 3] == radius && (k >= 3) == (simplified != 0));
		s_verbose = mine ? verbose : 0;
		if (mine)
		{
			printf("world %dx%d cells, %d bridges; ground then each bridge's view ('#' wall, '=' deck), y up, one char a cell\n", w, h, (int)bridges.size());
			for (size_t b = 0; b <= bridges.size(); ++b)
			{
				for (int y = h - 1; y >= 0; --y)
				{
					for (int x = 0; x < w; ++x)
					{
						if (b == 0)
							putchar(ground[y*w + x] ? '#' : '.');
						else
							putchar(bridges[b-1].deck[y*w + x] ? '=' : (bridges[b-1].view[y*w + x] ? '#' : '.'));
					}
					putchar('\n');
				}
				putchar('\n');
			}
		}
		bridgeFuzzWorld(tot, ground, w, h, bridges, RADIUS[k % 3], k >= 3 ? 1 : 0, 1, k >= 3 ? 0.36f : SHEET_SLACK, &failed);
		if (mine)
			printf("pairs %lld found %lld missed %lld phantom %lld off %lld close %lld deck %lld switch %lld longer %lld\n", tot.pairs, tot.found,
				tot.missed, tot.phantom, tot.offGround, tot.tooClose, tot.wrongDeck, tot.badSwitch, tot.longer);
	}
	s_verbose = 0;
	return 0;
}

static int bridgeFuzz( int count, unsigned int seed )
{
	for (int q = 0; q < 6; ++q)
		s_bridge[q].worstClearance = 99.0;
	s_seed = seed;
	int failures = 0;
	std::vector<unsigned char> ground;
	std::vector<FuzzBridge> bridges;
	static const int RADIUS[3] = { 0, 1, 3 };
	for (int n = 0; n < count; ++n)
	{
		int w, h;
		unsigned int worldSeed = s_seed;
		randomBridgeWorld(ground, w, h, bridges);
		if (getenv("NAVTEST_TRACE"))	// to find the world a crash or a hang is in
			fprintf(stderr, "world %d seed %u\n", n, worldSeed);
		for (int k = 0; k < 6; ++k)
		{
			bool failed = false;
			bool simplified = k >= 3;
			bridgeFuzzWorld(s_bridge[k], ground, w, h, bridges, RADIUS[k % 3], simplified ? 1 : 0, 1, simplified ? 0.36f : SHEET_SLACK, &failed);
			if (failed)
			{
				++failures;
				if (failures <= 5)
					printf("FAIL bridge world seed %u (%dx%d, %d bridges) radius %d half-cells, %s boundaries\n", worldSeed, w, h,
						(int)bridges.size(), RADIUS[k % 3], simplified ? "simplified" : "exact");
			}
		}
	}
	printf("bridges: %d worlds, %d failures\n", count, failures);
	static const char *NAME[6] = { "exact, radius 0  ", "exact, radius 0.5", "exact, radius 1.5", "tol 1, radius 0  ", "tol 1, radius 0.5", "tol 1, radius 1.5" };
	for (int r = 0; r < 6; ++r)
	{
		const BridgeTotals& t = s_bridge[r];
		printf("bridges, %s: %lld pairs, %lld with a path (%lld over a bridge); existence differs from the exhaustive search %lld (found a path that is not there %lld), "
					 "left its sheet %lld, nearer a wall than the radius %lld (nearest %.3f radii), on/off the bridge in the wrong place %lld, changed sheet off shared ground %lld, bad meshes %lld\n",
			NAME[r], t.pairs, t.found, t.viaBridge, t.missed + t.phantom, t.phantom, t.offGround, t.tooClose, (r % 3) ? t.worstClearance : 0.0,
			t.wrongDeck, t.badSwitch, t.invalidMesh);
		printf("    length / grid length: mean %.3f, worst %.3f, longer than the grid path %lld, by the fallback corridor %lld; %.1f nodes a search; a world: %.1f sheets, %.0f triangles, %.1f links\n",
			t.found ? t.ratioSum / t.found : 0.0, t.worstRatio, t.longer, t.fallbacks, t.pairs ? (double)t.expanded / t.pairs : 0.0,
			t.worlds ? (double)t.sheets / t.worlds : 0.0, t.worlds ? (double)t.tris / t.worlds : 0.0, t.worlds ? (double)t.links / t.worlds : 0.0);
	}
	return failures ? 1 : 0;
}

static const char *colorOf( int label )
{
	switch (label)
	{
		case 0: return "#e9e4d4";	// clear
		case 1: return "#9cc3e6";	// water
		case 2: return "#b08968";	// cliff
		case 3: return "#c9b8a0";	// rubble
		case 4: return "#5c5c5c";	// blocked
		case 5: return "#d98c5f";	// fence
		default: return "#ff00ff";
	}
}

static int svg( const char *gridPath, const char *outPath, int num, int den, int radiusHalfCells )
{
	FILE *fp = fopen(gridPath, "r");
	if (!fp)
	{
		printf("cannot open %s\n", gridPath);
		return 2;
	}
	int w = 0, h = 0;
	if (fscanf(fp, "%d %d", &w, &h) != 2 || w <= 0 || h <= 0)
	{
		printf("bad header in %s\n", gridPath);
		return 2;
	}
	std::vector<unsigned char> labels(w*h, 0);
	std::vector<char> row(w + 8);
	for (int y = 0; y < h; ++y)
	{
		if (fscanf(fp, "%s", &row[0]) != 1 || (int)strlen(&row[0]) != w)
		{
			printf("bad row %d in %s\n", y, gridPath);
			return 2;
		}
		for (int x = 0; x < w; ++x)
			labels[y*w + x] = (unsigned char)(row[x] - '0');
	}
	fclose(fp);

	Mesh mesh;
	clock_t t0 = clock();
	const int REPEAT = 5;
	for (int r = 0; r < REPEAT; ++r)
		buildMesh(&labels[0], w, h, num, den, mesh);
	double ms = 1000.0 * (double)(clock() - t0) / CLOCKS_PER_SEC / REPEAT;
	std::vector<char> report;
	int bad = validate(mesh, report);
	int uncovered = 0;
	int differ = compareWithGrid(mesh, labels, &uncovered);
	int perLabel[8] = {0,0,0,0,0,0,0,0};
	for (size_t t = 0; t < mesh.tris.size(); ++t)
		++perLabel[mesh.tris[t].label & 7];
	printf("NavMesh: grid=%dx%d tol=%d/%d verts=%d tris=%d clearTris=%d chains=%d points=%d->%d reverted=%d buildMs=%.1f "
				 "violations=%d cellsDiffer=%d (%.2f%%) uncovered=%d\n",
		w, h, num, den, (int)mesh.verts.size(), (int)mesh.tris.size(), perLabel[0], mesh.stats.chains, mesh.stats.exactPoints,
		mesh.stats.keptPoints, mesh.stats.revertedChains, ms, bad, differ, 100.0 * differ / (double)(w*h), uncovered);
	if (bad)
		printf("%s", &report[0]);

	// With a radius: the ground shrunk for a unit of that size (clear ground only), its mesh, and a path across the
	// map between the clear spots nearest two opposite corners. That mesh is what gets drawn.
	NavQuery::Path path;
	if (radiusHalfCells >= 0)
	{
		std::vector<unsigned char> shrunk;
		Mesh wide;
		t0 = clock();
		for (int r = 0; r < REPEAT; ++r)
		{
			erode(&labels[0], w, h, 1u, radiusHalfCells, shrunk);
			buildMesh(&shrunk[0], 2*w, 2*h, num, den, wide);
		}
		double wideMs = 1000.0 * (double)(clock() - t0) / CLOCKS_PER_SEC / REPEAT;
		std::vector<char> wideReport;
		int wideBad = validate(wide, wideReport);
		int W = 2*w, H = 2*h;
		// The biggest connected piece of the shrunk ground, then the two cells of it nearest opposite corners.
		std::vector<int> piece(W*H, -1);
		std::vector<int> stack;
		int bestPiece = -1, bestSize = 0, pieces = 0;
		for (int i = 0; i < W*H; ++i)
		{
			if (shrunk[i] != 0 || piece[i] >= 0)
				continue;
			int size = 0;
			piece[i] = pieces;
			stack.push_back(i);
			while (!stack.empty())
			{
				int c = stack.back(); stack.pop_back();
				++size;
				int cx = c % W, cy = c / W;
				static const int DX4[4] = { 1, -1, 0, 0 }, DY4[4] = { 0, 0, 1, -1 };
				for (int d = 0; d < 4; ++d)
				{
					int nx = cx + DX4[d], ny = cy + DY4[d];
					if (nx < 0 || ny < 0 || nx >= W || ny >= H || shrunk[ny*W + nx] != 0 || piece[ny*W + nx] >= 0)
						continue;
					piece[ny*W + nx] = pieces;
					stack.push_back(ny*W + nx);
				}
			}
			if (size > bestSize) { bestSize = size; bestPiece = pieces; }
			++pieces;
		}
		int from = -1, to = -1;
		long long fromD = 0, toD = 0;
		for (int i = 0; i < W*H; ++i)
		{
			if (piece[i] != bestPiece)
				continue;
			long long x = i % W, y = i / W;
			long long d1 = x*x + y*y, d2 = (W-x)*(W-x) + (H-y)*(H-y);
			if (from < 0 || d1 < fromD) { from = i; fromD = d1; }
			if (to < 0 || d2 < toD) { to = i; toD = d2; }
		}
		bool found = false;
		double pathMs = 0.0;
		if (from >= 0 && to >= 0)
		{
			NavQuery::Pt a = { (from % W) + 0.5f, (from / W) + 0.5f }, b = { (to % W) + 0.5f, (to / W) + 0.5f };
			t0 = clock();
			const int PATH_REPEAT = 50;
			for (int r = 0; r < PATH_REPEAT; ++r)
				found = NavQuery::findPath(wide, a, b, 1u, path);
			pathMs = 1000.0 * (double)(clock() - t0) / CLOCKS_PER_SEC / PATH_REPEAT;
		}
		printf("NavMeshWide: radius=%.1f cells tris=%d erodeAndBuildMs=%.1f violations=%d groundPieces=%d path=%d length=%.1f cells corners=%d "
					 "searchMs=%.2f nodes=%d corridors=%d\n",
			0.5 * radiusHalfCells, (int)wide.tris.size(), wideMs, wideBad, pieces, found ? 1 : 0, path.length * 0.5,
			(int)path.pts.size(), pathMs, path.expanded, path.corridors);
		if (wideBad)
			bad += wideBad;
		mesh = wide;
		w = W;
		h = H;
	}

	FILE *out = fopen(outPath, "w");
	if (!out)
	{
		printf("cannot write %s\n", outPath);
		return 2;
	}
	const int S = radiusHalfCells >= 0 ? 2 : 4;	// pixels per mesh cell
	fprintf(out, "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 %d %d\" width=\"%d\" height=\"%d\">\n", w*S, h*S, w*S, h*S);
	for (size_t t = 0; t < mesh.tris.size(); ++t)
	{
		const Tri& tr = mesh.tris[t];
		fprintf(out, "<path d=\"M%d %d L%d %d L%d %dZ\" fill=\"%s\" stroke=\"#00000030\" stroke-width=\"0.3\"/>\n",
			mesh.verts[tr.v[0]].x*S, (h - mesh.verts[tr.v[0]].y)*S, mesh.verts[tr.v[1]].x*S, (h - mesh.verts[tr.v[1]].y)*S,
			mesh.verts[tr.v[2]].x*S, (h - mesh.verts[tr.v[2]].y)*S, colorOf(tr.label));
	}
	for (size_t t = 0; t < mesh.tris.size(); ++t)
	{
		const Tri& tr = mesh.tris[t];
		for (int i = 0; i < 3; ++i)
		{
			if (tr.c[i] && (tr.n[i] < 0 || tr.n[i] > (int)t))
				fprintf(out, "<path d=\"M%d %d L%d %d\" stroke=\"#202020\" stroke-width=\"0.8\"/>\n",
					mesh.verts[tr.v[i]].x*S, (h - mesh.verts[tr.v[i]].y)*S, mesh.verts[tr.v[(i+1)%3]].x*S, (h - mesh.verts[tr.v[(i+1)%3]].y)*S);
		}
	}
	if (path.pts.size() >= 2)
	{
		fprintf(out, "<path d=\"");
		for (size_t k = 0; k < path.pts.size(); ++k)
			fprintf(out, "%c%.1f %.1f ", k ? 'L' : 'M', path.pts[k].x*S, (h - path.pts[k].y)*S);
		fprintf(out, "\" fill=\"none\" stroke=\"#d62020\" stroke-width=\"2.5\"/>\n");
	}
	fprintf(out, "</svg>\n");
	fclose(out);
	return bad ? 1 : 0;
}

int main( int argc, char **argv )
{
	if (argc >= 3 && !strcmp(argv[1], "fuzz"))
		return fuzz(atoi(argv[2]), argc >= 4 ? (unsigned int)atoi(argv[3]) : 12345u);
	if (argc >= 3 && !strcmp(argv[1], "bridges"))
		return bridgeFuzz(atoi(argv[2]), argc >= 4 ? (unsigned int)atoi(argv[3]) : 12345u);
	if (argc >= 5 && !strcmp(argv[1], "bridgeworld"))
		return bridgeWorld((unsigned int)strtoul(argv[2], 0, 10), atoi(argv[3]), atoi(argv[4]), argc >= 6 ? atoi(argv[5]) : 1);
	if (argc >= 6 && !strcmp(argv[1], "svg"))
		return svg(argv[2], argv[3], atoi(argv[4]), atoi(argv[5]), argc >= 7 ? atoi(argv[6]) : -1);
	printf("usage: navtest fuzz <count> [seed] | navtest bridges <count> [seed] | navtest svg <grid.txt> <out.svg> <num> <den> [radiusHalfCells]\n");
	return 2;
}
