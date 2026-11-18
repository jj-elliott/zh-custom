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

// zh-pipeline @feature 2026-11-18 Navigation mesh overlay, see W3DNavMeshOverlay.h.
//
// What is drawn (all of it translucent, on top of the scene, within OVERLAY_RANGE of where the camera looks):
//   green     ground the selected unit's centre may be on (the mesh of its size: ground shrunk by its radius)
//   blue      a bridge's deck
//   red       walls: everything that mesh has as impassable (not drawn with navmesh.overlay=2)
//   dark lines   the boundaries between the above; pale lines the other triangle edges
//   yellow    the selected units' current paths, node to node; cyan where a node is on a bridge's layer
// With nothing selected: whichever set of meshes was built first. A set exists only once a unit of that kind has
// asked for a path, and follows the grid at that kind's next request: the overlay shows what the sim has and
// never makes it build anything.

#include "W3DDevice/GameClient/W3DNavMeshOverlay.h"

#include "GameClient/Drawable.h"
#include "GameClient/InGameUI.h"
#include "GameClient/View.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/NavMesh.h"
#include "GameLogic/Object.h"
#include "GameLogic/TerrainLogic.h"
#include "WW3D2/dx8wrapper.h"

#include <vector>

// No texture, no depth test or write, alpha blending, both sides.
#define SC_OVERLAY ( SHADE_CNST(ShaderClass::PASS_ALWAYS, ShaderClass::DEPTH_WRITE_DISABLE, ShaderClass::COLOR_WRITE_ENABLE, ShaderClass::SRCBLEND_SRC_ALPHA, \
	ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA, ShaderClass::FOG_DISABLE, ShaderClass::GRADIENT_MODULATE, ShaderClass::SECONDARY_GRADIENT_DISABLE, ShaderClass::TEXTURING_ENABLE, \
	ShaderClass::ALPHATEST_DISABLE, ShaderClass::CULL_MODE_DISABLE, \
	ShaderClass::DETAILCOLOR_DISABLE, ShaderClass::DETAILALPHA_DISABLE) )

namespace
{
	const Real OVERLAY_RANGE = 900.0f;	// world units from where the camera looks
	const Real LIFT = 1.5f;							// above the ground, so that it reads as lying on it
	const Int MAX_PATHS = 60;

	struct OverlayVertex
	{
		Real x, y, z;
		unsigned color;	// a<<24 | r<<16 | g<<8 | b
	};

	inline unsigned colorOf( Int r, Int g, Int b, Int a )
	{
		return ((unsigned)a << 24) | ((unsigned)r << 16) | ((unsigned)g << 8) | (unsigned)b;
	}

	inline void addVertex( std::vector<OverlayVertex>& out, Real x, Real y, Real z, unsigned color )
	{
		OverlayVertex v;
		v.x = x;
		v.y = y;
		v.z = z;
		v.color = color;
		out.push_back(v);
	}

	/// A line as a thin quad lying flat.
	void addLine( std::vector<OverlayVertex>& out, const Coord3D& a, const Coord3D& b, Real width, unsigned color )
	{
		Real dx = b.x - a.x, dy = b.y - a.y;
		Real len = WWMath::Sqrt(dx*dx + dy*dy);
		if (len < 0.001f)
			return;
		Real nx = -dy / len * width * 0.5f, ny = dx / len * width * 0.5f;
		addVertex(out, a.x - nx, a.y - ny, a.z, color);
		addVertex(out, b.x - nx, b.y - ny, b.z, color);
		addVertex(out, b.x + nx, b.y + ny, b.z, color);
		addVertex(out, a.x - nx, a.y - ny, a.z, color);
		addVertex(out, b.x + nx, b.y + ny, b.z, color);
		addVertex(out, a.x + nx, a.y + ny, a.z, color);
	}

	void addSquare( std::vector<OverlayVertex>& out, const Coord3D& c, Real half, unsigned color )
	{
		addVertex(out, c.x - half, c.y - half, c.z, color);
		addVertex(out, c.x + half, c.y - half, c.z, color);
		addVertex(out, c.x + half, c.y + half, c.z, color);
		addVertex(out, c.x - half, c.y - half, c.z, color);
		addVertex(out, c.x + half, c.y + half, c.z, color);
		addVertex(out, c.x - half, c.y + half, c.z, color);
	}

	/// The meshes: filled triangles, then their edges.
	void addSheets( std::vector<OverlayVertex>& fill, std::vector<OverlayVertex>& lines, const std::vector<NavMesh::DebugSheet>& sheets,
									const Coord3D& centre, Bool walls, Bool upToDate )
	{
		const Real HALF = PATHFIND_CELL_SIZE_F * 0.5f;
		Region3D extent;
		TheTerrainLogic->getExtent(&extent);
		for (size_t s = 0; s < sheets.size(); ++s)
		{
			const NavBuild::Mesh& mesh = *sheets[s].mesh;
			PathfindLayerEnum layer = (PathfindLayerEnum)sheets[s].layer;
			for (size_t t = 0; t < mesh.tris.size(); ++t)
			{
				const NavBuild::Tri& tr = mesh.tris[t];
				if (tr.label == NavBuild::SHEET_OTHER || (tr.label == NavBuild::SHEET_WALL && (!walls || s > 0)))
					continue;	// ground another sheet draws; a bridge sheet's walls are the ground sheet's too
				Coord3D p[3];
				Real loX = 0, hiX = 0, loY = 0, hiY = 0;
				Int i;
				for (i = 0; i < 3; ++i)
				{
					p[i].x = (Real)(mesh.verts[tr.v[i]].x + sheets[s].ox) * HALF;
					p[i].y = (Real)(mesh.verts[tr.v[i]].y + sheets[s].oy) * HALF;
					if (i == 0 || p[i].x < loX) loX = p[i].x;
					if (i == 0 || p[i].x > hiX) hiX = p[i].x;
					if (i == 0 || p[i].y < loY) loY = p[i].y;
					if (i == 0 || p[i].y > hiY) hiY = p[i].y;
				}
				if (hiX < centre.x - OVERLAY_RANGE || loX > centre.x + OVERLAY_RANGE || hiY < centre.y - OVERLAY_RANGE || loY > centre.y + OVERLAY_RANGE)
					continue;
				Bool deck = (tr.label == NavBuild::SHEET_DECK);
				for (i = 0; i < 3; ++i)
				{
					// Heights from just inside the map: its border has none.
					Real hx = p[i].x, hy = p[i].y;
					if (hx < extent.lo.x + 1.0f) hx = extent.lo.x + 1.0f;
					if (hx > extent.hi.x - 1.0f) hx = extent.hi.x - 1.0f;
					if (hy < extent.lo.y + 1.0f) hy = extent.lo.y + 1.0f;
					if (hy > extent.hi.y - 1.0f) hy = extent.hi.y - 1.0f;
					p[i].z = (deck ? TheTerrainLogic->getLayerHeight(hx, hy, layer) : TheTerrainLogic->getGroundHeight(hx, hy)) + LIFT;
				}
				unsigned color;
				if (deck)
					color = colorOf(60, 140, 255, 80);
				else if (tr.label == NavBuild::SHEET_GROUND)
					color = upToDate ? colorOf(40, 210, 90, 50) : colorOf(210, 200, 40, 50);	// yellowish: the grid has changed since this was built
				else
					color = colorOf(230, 40, 40, 45);
				for (i = 0; i < 3; ++i)
					addVertex(fill, p[i].x, p[i].y, p[i].z, color);

				// Edges, each once: of ground and deck triangles only (a wall's own inner edges say nothing).
				if (tr.label == NavBuild::SHEET_WALL)
					continue;
				for (i = 0; i < 3; ++i)
				{
					Int nb = tr.n[i];
					Bool open = nb >= 0 && (mesh.tris[nb].label == NavBuild::SHEET_GROUND || mesh.tris[nb].label == NavBuild::SHEET_DECK);
					if (open && nb < (Int)t)
						continue;
					if (open && mesh.tris[nb].label == tr.label)
						addLine(lines, p[i], p[(i+1)%3], 0.35f, colorOf(255, 255, 255, 70));
					else if (open)
						addLine(lines, p[i], p[(i+1)%3], 0.8f, colorOf(20, 60, 160, 200));	// where the deck meets the ground
					else if (nb >= 0 && mesh.tris[nb].label == NavBuild::SHEET_OTHER)
						addLine(lines, p[i], p[(i+1)%3], 0.5f, colorOf(255, 255, 255, 110));	// where two sheets' ground meets
					else
						addLine(lines, p[i], p[(i+1)%3], 1.0f, colorOf(30, 30, 30, 220));	// a wall
				}
			}
		}
	}

	void addPath( std::vector<OverlayVertex>& lines, const Path *path )
	{
		const PathNode *prev = nullptr;
		// (Path's accessors are not const; nothing is changed.)
		for (const PathNode *node = const_cast<Path *>(path)->getFirstNode(); node != nullptr; node = node->getNext())
		{
			Coord3D here = *node->getPosition();
			here.z += LIFT + 1.0f;
			Bool onBridge = node->getLayer() > LAYER_GROUND;
			if (prev != nullptr)
			{
				Coord3D before = *prev->getPosition();
				before.z += LIFT + 1.0f;
				Bool bridge = onBridge && prev->getLayer() > LAYER_GROUND;
				addLine(lines, before, here, 1.4f, bridge ? colorOf(40, 230, 255, 235) : colorOf(255, 225, 0, 235));
			}
			addSquare(lines, here, 1.6f, onBridge ? colorOf(40, 230, 255, 255) : colorOf(255, 140, 0, 255));
			prev = node;
		}
	}

	void draw( const std::vector<OverlayVertex>& verts )
	{
		const size_t BATCH = 15000;	// vertices: 5000 triangles
		for (size_t first = 0; first < verts.size(); first += BATCH)
		{
			size_t count = verts.size() - first;
			if (count > BATCH)
				count = BATCH;
			DynamicVBAccessClass vb_access(BUFFER_TYPE_DYNAMIC_DX8, DX8_FVF_XYZNDUV2, (unsigned short)count);
			DynamicIBAccessClass ib_access(BUFFER_TYPE_DYNAMIC_DX8, (unsigned short)count);
			{
				DynamicVBAccessClass::WriteLockClass lock(&vb_access);
				VertexFormatXYZNDUV2 *vb = lock.Get_Formatted_Vertex_Array();
				DynamicIBAccessClass::WriteLockClass lockib(&ib_access);
				UnsignedShort *ib = lockib.Get_Index_Array();
				if (!vb || !ib)
					return;
				for (size_t k = 0; k < count; ++k)
				{
					const OverlayVertex& v = verts[first + k];
					vb->x = v.x;
					vb->y = v.y;
					vb->z = v.z;
					vb->nx = 0.0f;
					vb->ny = 0.0f;
					vb->nz = 1.0f;
					vb->diffuse = v.color;
					vb->u1 = 0;
					vb->v1 = 0;
					vb->u2 = 0;
					vb->v2 = 0;
					++vb;
					*ib++ = (UnsignedShort)k;
				}
			}
			DX8Wrapper::Set_Shader(ShaderClass(SC_OVERLAY));
			DX8Wrapper::Set_Index_Buffer(ib_access, 0);
			DX8Wrapper::Set_Vertex_Buffer(vb_access);
			DX8Wrapper::Draw_Triangles(0, (unsigned short)(count / 3), 0, (unsigned short)count);
		}
	}
}

W3DNavMeshOverlay::~W3DNavMeshOverlay()
{
	REF_PTR_RELEASE(m_vertexMaterialClass);
}

W3DNavMeshOverlay::W3DNavMeshOverlay()
{
	m_vertexMaterialClass = VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
}

bool W3DNavMeshOverlay::Cast_Ray(RayCollisionTestClass & raytest)
{
	return false;
}

W3DNavMeshOverlay::W3DNavMeshOverlay(const W3DNavMeshOverlay & src)
{
	m_vertexMaterialClass = VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
}

W3DNavMeshOverlay & W3DNavMeshOverlay::operator = (const W3DNavMeshOverlay & that)
{
	return *this;
}

void W3DNavMeshOverlay::Get_Obj_Space_Bounding_Sphere(SphereClass & sphere) const
{
	// Everywhere: it is never culled as an object; what it draws is chosen in Render.
	Vector3 ObjSpaceCenter(0.0f, 0.0f, 0.0f);
	sphere.Init(ObjSpaceCenter, 100000.0f);
}

void W3DNavMeshOverlay::Get_Obj_Space_Bounding_Box(AABoxClass & box) const
{
	Vector3 minPt(-100000.0f, -100000.0f, -1000.0f);
	Vector3 maxPt(100000.0f, 100000.0f, 10000.0f);
	box.Init(minPt, maxPt);
}

Int W3DNavMeshOverlay::Class_ID() const
{
	return RenderObjClass::CLASSID_UNKNOWN;
}

RenderObjClass * W3DNavMeshOverlay::Clone() const
{
	return NEW W3DNavMeshOverlay(*this);
}

/** Render draws into the current 3d context. */
void W3DNavMeshOverlay::Render(RenderInfoClass & rinfo)
{
	Int mode = NavMesh::debugOverlay();
	if (mode == 0 || TheTerrainLogic == nullptr || TheInGameUI == nullptr || TheTacticalView == nullptr)
		return;
	// After the opaque scene, like the debug icons.
	if (WW3D::Are_Static_Sort_Lists_Enabled())
	{
		WW3D::Add_To_Static_Sort_List(this, 1);
		return;
	}

	// The mesh of the first selected ground unit's size, and every selected unit's path.
	std::vector<OverlayVertex> fill, lines;
	std::vector<NavMesh::DebugSheet> sheets;
	const Object *meshOf = nullptr;
	const DrawableList *selected = TheInGameUI->getAllSelectedDrawables();
	Int paths = 0;
	if (selected != nullptr)
	{
		for (DrawableList::const_iterator it = selected->begin(); it != selected->end(); ++it)
		{
			const Drawable *draw = *it;
			const Object *obj = draw ? draw->getObject() : nullptr;
			const AIUpdateInterface *ai = obj ? obj->getAI() : nullptr;
			if (ai == nullptr)
				continue;
			if (meshOf == nullptr && ai->isDoingGroundMovement())
				meshOf = obj;
			if (ai->getPath() != nullptr && paths < MAX_PATHS)
			{
				addPath(lines, ai->getPath());
				++paths;
			}
		}
	}
	Coord3D centre = TheTacticalView->getPosition();
	Bool upToDate = true;
	Bool have = false;
	if (meshOf != nullptr)
		have = NavMesh::debugSheets(meshOf, &meshOf->getAI()->getLocomotorSet(), sheets, nullptr, &upToDate);
	if (!have)
		have = NavMesh::debugSheets(nullptr, nullptr, sheets, nullptr, &upToDate);
	std::vector<OverlayVertex> meshLines;
	if (have)
		addSheets(fill, meshLines, sheets, centre, mode != 2, upToDate);
	if (fill.empty() && meshLines.empty() && lines.empty())
		return;

	DX8Wrapper::Apply_Render_State_Changes();
	DX8Wrapper::Set_Material(m_vertexMaterialClass);
	DX8Wrapper::Set_Texture(0, nullptr);
	DX8Wrapper::Apply_Render_State_Changes();
	DX8Wrapper::Set_Transform(D3DTS_WORLD, Transform);
	draw(fill);
	draw(meshLines);
	draw(lines);	// the paths last, on top
}
