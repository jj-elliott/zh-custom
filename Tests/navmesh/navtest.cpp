// zh-pipeline: standalone test of the navigation mesh builder (Core/GameEngine/.../NavMeshBuild.cpp), no engine.
// Built and run by scripts/navtest.ps1.
//   navtest fuzz <count> [seed]              random label grids, every tolerance, full validation
//   navtest svg <grid.txt> <out.svg> <num> <den>   build one grid (as dumped by the engine), draw it, print statistics
// Grid file: "width height" then height lines of width characters, '0'..'9' = label, first line = y 0.

#include "GameLogic/NavMeshBuild.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

using namespace NavBuild;

static unsigned int s_seed = 1;
static unsigned int rnd()
{
	s_seed = s_seed * 1664525u + 1013904223u;
	return s_seed >> 8;
}

// For every cell, the label of the triangle that holds its centre. Returns how many differ from the grid.
static int compareWithGrid( const Mesh& mesh, const std::vector<unsigned char>& labels, int *uncovered )
{
	std::vector<unsigned char> got(labels.size(), 255);
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
					got[y*mesh.width + x] = tr.label;
			}
		}
	}
	int differ = 0;
	*uncovered = 0;
	for (size_t i = 0; i < labels.size(); ++i)
	{
		if (got[i] == 255)
			++*uncovered;
		else if (got[i] != labels[i])
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

static int fuzz( int count, unsigned int seed )
{
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
			if (k == 0 && differ)
				++bad;
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
		printf("  tol %d/%d: %lld triangles, %.2f%% of cells on the other side of a simplified boundary, %lld chains reverted\n",
			TOL[k][0], TOL[k][1], totalTris[k], 100.0 * (double)totalDiffer[k] / (double)totalCells, reverted[k]);
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

static int svg( const char *gridPath, const char *outPath, int num, int den )
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

	FILE *out = fopen(outPath, "w");
	if (!out)
	{
		printf("cannot write %s\n", outPath);
		return 2;
	}
	const int S = 4;	// pixels per cell
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
	fprintf(out, "</svg>\n");
	fclose(out);
	return bad ? 1 : 0;
}

int main( int argc, char **argv )
{
	if (argc >= 3 && !strcmp(argv[1], "fuzz"))
		return fuzz(atoi(argv[2]), argc >= 4 ? (unsigned int)atoi(argv[3]) : 12345u);
	if (argc >= 6 && !strcmp(argv[1], "svg"))
		return svg(argv[2], argv[3], atoi(argv[4]), atoi(argv[5]));
	printf("usage: navtest fuzz <count> [seed] | navtest svg <grid.txt> <out.svg> <num> <den>\n");
	return 2;
}
