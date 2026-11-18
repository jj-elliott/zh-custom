// zh-pipeline: standalone test of the navigation mesh builder (Core/GameEngine/.../NavMeshBuild.cpp), no engine.
// Built and run by scripts/navtest.ps1.
//   navtest fuzz <count> [seed]              random label grids, every tolerance, full validation
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
	if (argc >= 6 && !strcmp(argv[1], "svg"))
		return svg(argv[2], argv[3], atoi(argv[4]), atoi(argv[5]), argc >= 7 ? atoi(argv[6]) : -1);
	printf("usage: navtest fuzz <count> [seed] | navtest svg <grid.txt> <out.svg> <num> <den> [radiusHalfCells]\n");
	return 2;
}
