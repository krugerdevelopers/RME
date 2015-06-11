
#include "main.h"

#include "editor.h"
#include "gui.h"
#include "sprites.h"
#include "map_drawer.h"
#include "map_display.h"
#include "copybuffer.h"
#include "live_socket.h"

#include "doodad_brush.h"
#include "creature_brush.h"
#include "house_exit_brush.h"
#include "house_brush.h"
#include "spawn_brush.h"
#include "wall_brush.h"
#include "carpet_brush.h"
#include "raw_brush.h"
#include "table_brush.h"
#include "waypoint_brush.h"

MapDrawer::MapDrawer(const DrawingOptions& options, MapCanvas* canvas, wxPaintDC& pdc) : canvas(canvas), editor(canvas->editor), pdc(pdc), options(options)
{
	canvas->MouseToMap(&mouse_map_x, &mouse_map_y);
	canvas->GetViewBox(&view_scroll_x, &view_scroll_y, &screensize_x, &screensize_y);

	dragging = canvas->dragging;
	dragging_draw = canvas->dragging_draw;

	zoom = canvas->GetZoom();
	tile_size = int(32/zoom); // after zoom
	floor = canvas->GetFloor();
	
	SetupVars();
	SetupGL();
}

void MapDrawer::SetupVars()
{
	if(options.show_all_floors)
	{
		if(floor < 8)
			start_z = 7;
		else
			start_z = std::min(15, floor + 2);
	}
	else
		start_z = floor;
	
	end_z = floor;
	superend_z = (floor > 7? 8 : 0);

	start_x = view_scroll_x / 32;
	start_y = view_scroll_y / 32;

	if(floor > 7)
	{
		start_x -= 2;
		start_y -= 2;
	}

	end_x = start_x + screensize_x / tile_size + 2;
	end_y = start_y + screensize_y / tile_size + 2;
}

void MapDrawer::SetupGL()
{
	//
	glViewport(0, 0, screensize_x, screensize_y);

	// Enable 2D mode
	int vPort[4];

	glGetIntegerv(GL_VIEWPORT, vPort);

	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glOrtho(0, vPort[2]*zoom, vPort[3]*zoom, 0, -1, 1);

	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();
	glTranslatef(0.375f, 0.375f, 0.0f);
}

DrawingOptions::DrawingOptions()
{
	SetDefault();
}

void DrawingOptions::SetDefault()
{
	transparent_floors = false;
	transparent_items = false;
	show_ingame_box = false;
	ingame = false;
	dragging = false;

	show_grid = 0;
	show_all_floors = true;
	show_creatures = true;
	show_spawns = true;
	show_houses = true;
	show_shade = true;
	show_special_tiles = true;
	show_items = true;

	highlight_items = false;
	show_blocking = false;
	show_only_colors = false;
	show_only_modified = false;
	hide_items_when_zoomed = true;
}

void DrawingOptions::SetIngame()
{
	transparent_floors = false;
	transparent_items = false;
	show_ingame_box = false;
	ingame = true;
	dragging = false;

	show_grid = 0;
	show_all_floors = true;
	show_creatures = true;
	show_spawns = false;
	show_houses = false;
	show_shade = false;
	show_special_tiles = false;
	show_items = true;

	highlight_items = false;
	show_blocking = false;
	show_only_colors = false;
	show_only_modified = false;
	hide_items_when_zoomed = false;
}

MapDrawer::~MapDrawer()
{
	// Disable 2D mode
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
}

void MapDrawer::Draw()
{
	DrawBackground();
	DrawMap();
	DrawDraggingShadow();
	DrawHigherFloors();
	if(options.dragging)
		DrawSelectionBox();
	DrawLiveCursors();
	DrawBrush();
	DrawIngameBox();
	if(options.show_grid)
		DrawGrid();
	//DrawTooltips();
}

void MapDrawer::DrawBackground()
{
	// Black Background
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glLoadIdentity();

	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_BLEND);

	//glAlphaFunc(GL_GEQUAL, 0.9f);
	//glEnable(GL_ALPHA_TEST);
}

inline int getFloorAdjustment(int floor)
{
	if(floor > 7) // Underground
		return 0; // No adjustment
	else
		return 32*(7-floor);
}

void MapDrawer::DrawMap()
{
	bool live_client = editor.IsLiveClient();

	// The current house we're drawing
	current_house_id = 0;
	if(HouseBrush* hb = dynamic_cast<HouseBrush*>(gui.GetCurrentBrush())) {
		current_house_id = hb->getHouseID();
	} else if(HouseExitBrush* heb = dynamic_cast<HouseExitBrush*>(gui.GetCurrentBrush())) {
		current_house_id = heb->getHouseID();
	}

	// Enable texture mode
	if(!options.show_only_colors)
		glEnable(GL_TEXTURE_2D);
	
	for(int map_z = start_z; map_z >= superend_z; map_z--)
	{
		if(map_z == end_z && start_z != end_z && options.show_shade)
		{
			// Draw shade
			if(!options.show_only_colors)
				glDisable(GL_TEXTURE_2D);

			glColor4ub(0, 0, 0, 128);
			glBegin(GL_QUADS);
				glVertex2f(0,int(screensize_y*zoom));
				glVertex2f(int(screensize_x*zoom),int(screensize_y*zoom));
				glVertex2f(int(screensize_x*zoom),0);
				glVertex2f(0,0);
			glEnd();

			if(!options.show_only_colors)
				glEnable(GL_TEXTURE_2D);
		}

		if(map_z >= end_z) {
			int nd_start_x = start_x & ~3;
			int nd_start_y = start_y & ~3;
			int nd_end_x = (end_x & ~3) + 4;
			int nd_end_y = (end_y & ~3) + 4;

			for(int nd_map_x = nd_start_x; nd_map_x <= nd_end_x; nd_map_x += 4) {
				for(int nd_map_y = nd_start_y; nd_map_y <= nd_end_y; nd_map_y += 4) {
					QTreeNode* nd = editor.map.getLeaf(nd_map_x, nd_map_y);
					if(!nd)
					{
						if(live_client)
						{
							nd = editor.map.createLeaf(nd_map_x, nd_map_y);
							nd->setVisible(false, false);
						}
						else 
							continue;
					}

					if(!live_client || nd->isVisible(map_z > 7))
					{
						for(int map_x = 0; map_x < 4; ++map_x)
						{
							for(int map_y = 0; map_y < 4; ++map_y)
							{
								DrawTile(nd->getTile(map_x, map_y, map_z));
							}
						}
					}
					else
					{
						if(!nd->isRequested(map_z > 7))
						{
							// Request the node
							editor.QueryNode(nd_map_x, nd_map_y, map_z > 7);
							nd->setRequested(map_z > 7, true);
						}
						int cy = (nd_map_y)*32-view_scroll_y - getFloorAdjustment(floor);
						int cx = (nd_map_x)*32-view_scroll_x - getFloorAdjustment(floor);

						glColor4ub(255, 0, 255, 128);
						glBegin(GL_QUADS);
							glVertex2f(cx,     cy+32*4);
							glVertex2f(cx+32*4,cy+32*4);
							glVertex2f(cx+32*4,cy);
							glVertex2f(cx,     cy);
						glEnd();
					}
				}
			}
		}

		if(options.show_only_colors)
			glEnable(GL_TEXTURE_2D);

		// Draws the doodad preview or the paste preview (or import preview)
		if(gui.secondary_map != nullptr && options.ingame == false)
		{
			Position normalPos;
			Position to(mouse_map_x, mouse_map_y, floor);

			if(canvas->isPasting())
			{
				normalPos = editor.copybuffer.getPosition();
			}
			else if(dynamic_cast<DoodadBrush*>(gui.GetCurrentBrush()))
			{
				normalPos = Position(0x8000, 0x8000, 0x8);
			}

			for(int map_x = start_x; map_x <= end_x; map_x++)
			{
				for(int map_y = start_y; map_y <= end_y; map_y++)
				{
					Position final(map_x, map_y, map_z);
					Position pos = normalPos + final - to;
					//Position pos = topos + copypos - Position(map_x, map_y, map_z);
					if(pos.z >= MAP_HEIGHT || pos.z < 0)
					{
						continue;
					}
					Tile* tile = gui.secondary_map->getTile(pos);

					if(tile)
					{
						int draw_x = map_x*32 - view_scroll_x;
						int draw_y = map_y*32 - view_scroll_y;

						// Compensate for underground/overground
						if(map_z <= 7)
							draw_x -= (7-map_z)*32;
						else
							draw_x -= 32*(floor - map_z);
						if(map_z <= 7)
							draw_y -= (7-map_z)*32;
						else
							draw_y -= 32*(floor - map_z);

						// Draw ground
						uint8_t r = 160, g = 160, b = 160;
						if(tile->ground)
						{
							if(tile->isBlocking() && options.show_blocking)
							{
								g = g/3*2;
								b = b/3*2;
							}
							if(tile->isHouseTile() && options.show_houses)
							{
								if((int)tile->getHouseID() == current_house_id) {
									r /= 2;
								} else {
									r /= 2;
									g /= 2;
								}
							} else if(options.show_special_tiles && tile->isPZ()) {
								r /= 2;
								b /= 2;
							}
							if(options.show_special_tiles && tile->getMapFlags() & TILESTATE_PVPZONE) {
								r = r/3*2;
								b = r/3*2;
							}
							if(options.show_special_tiles && tile->getMapFlags() & TILESTATE_NOLOGOUT) {
								b /= 2;
							}
							if(options.show_special_tiles && tile->getMapFlags() & TILESTATE_NOPVP) {
								g /= 2;
							}
							BlitItem(draw_x, draw_y, tile, tile->ground, true, r, g, b, 160);
						}

						// Draw items on the tile
						if(zoom <= 10.0 || options.hide_items_when_zoomed == false)
						{
							ItemVector::iterator it;
							for(it = tile->items.begin(); it != tile->items.end(); it++)
							{
								if((*it)->isBorder()) {
									BlitItem(draw_x, draw_y, tile, *it, true, 160, r, g, b);
								} else {
									BlitItem(draw_x, draw_y, tile, *it, true, 160, 160, 160, 160);
								}
							}
							if(tile->creature && options.show_creatures)
								BlitCreature(draw_x, draw_y, tile->creature);
						}
					}
				}
			}
		}

		start_x -= 1; end_x += 1;
		start_y -= 1; end_y += 1;
	}

	if(!options.show_only_colors)
		glEnable(GL_TEXTURE_2D);
}

void MapDrawer::DrawIngameBox()
{
	if(!options.show_ingame_box)
		return;

	int center_x = start_x + int(screensize_x * zoom / 64);
	int center_y = start_y + int(screensize_y * zoom / 64);
	
	int box_start_map_x = center_x;
	int box_start_map_y = center_y + 1;
	int box_end_map_x = center_x + 18;
	int box_end_map_y = center_y + 15;
	
	int box_start_x = box_start_map_x * 32 - view_scroll_x;
	int box_start_y = box_start_map_y * 32 - view_scroll_y;
	int box_end_x = box_end_map_x * 32 - view_scroll_x;
	int box_end_y = box_end_map_y * 32 - view_scroll_y;

	if(box_start_map_x >= start_x)
	{
		glColor4ub(0, 0, 0, 128);
		glBegin(GL_QUADS);
				glVertex2f(0, screensize_y * zoom);
				glVertex2f(box_start_x, screensize_y * zoom);
				glVertex2f(box_start_x, 0);
				glVertex2f(0, 0);
		glEnd();
	}

	if(box_end_map_x < end_x)
	{
		glColor4ub(0, 0, 0, 128);
		glBegin(GL_QUADS);
				glVertex2f(box_end_x, screensize_y * zoom);
				glVertex2f(screensize_x * zoom, screensize_y * zoom);
				glVertex2f(screensize_x * zoom, 0);
				glVertex2f(box_end_x, 0);
		glEnd();
	}

	if(box_start_map_y >= start_y)
	{
		glColor4ub(0, 0, 0, 128);
		glBegin(GL_QUADS);
				glVertex2f(box_start_x, box_start_y);
				glVertex2f(box_end_x, box_start_y);
				glVertex2f(box_end_x, 0);
				glVertex2f(box_start_x, 0);
		glEnd();
	}

	if(box_end_map_y < end_y)
	{
		glColor4ub(0, 0, 0, 128);
		glBegin(GL_QUADS);
				glVertex2f(box_start_x, screensize_y * zoom);
				glVertex2f(box_end_x, screensize_y * zoom);
				glVertex2f(box_end_x, box_end_y);
				glVertex2f(box_start_x, box_end_y);
		glEnd();
	}
}

void MapDrawer::DrawGrid()
{
	for(int y = start_y; y < end_y; ++y)
	{
		glColor4ub(255, 255, 255, 128);
		glBegin(GL_LINES);
			glVertex2f(start_x * 32 - view_scroll_x, y * 32 - view_scroll_y);
			glVertex2f(end_x * 32 - view_scroll_x,   y * 32 - view_scroll_y);
		glEnd();
	}

	for(int x = start_x; x < end_x; ++x)
	{
		glColor4ub(255, 255, 255, 128);
		glBegin(GL_LINES);
			glVertex2f(x * 32 - view_scroll_x, start_y * 32 - view_scroll_y);
			glVertex2f(x * 32 - view_scroll_x, end_y * 32 - view_scroll_y);
		glEnd();
	}
}

void MapDrawer::DrawDraggingShadow()
{
	glEnable(GL_TEXTURE_2D);

	// Draw dragging shadow
	if(!editor.selection.isBusy() && dragging && !options.ingame) {
		for(TileVector::iterator tit = editor.selection.begin(); tit != editor.selection.end(); tit++) {
			Tile* tile = *tit;
			Position pos = tile->getPosition();

			int move_x, move_y, move_z;
			move_x = canvas->drag_start_x - mouse_map_x;
			move_y = canvas->drag_start_y - mouse_map_y;
			move_z = canvas->drag_start_z - floor;

			pos.x -= move_x;
			pos.y -= move_y;
			pos.z -= move_z;

			if(pos.z < 0 || pos.z >= MAP_HEIGHT) {
				continue;
			}

			// On screen and dragging?
			if(pos.x+2 > start_x && pos.x < end_x &&
				pos.y+2 > start_y && pos.y < end_y &&
				(move_x != 0 || move_y != 0 || move_z != 0)
				)
			{
				int draw_x = pos.x*32 - view_scroll_x;

				if(pos.z <= 7) 
					draw_x -= (7-pos.z)*32;
				else draw_x -= 32*(floor - pos.z);

				int draw_y = pos.y*32 - view_scroll_y;
				if(pos.z <= 7)
					draw_y -= (7-pos.z)*32;
				else draw_y -= 32*(floor - pos.z);

				ItemVector toRender = tile->getSelectedItems();
				Tile* desttile = editor.map.getTile(pos);
				for(ItemVector::const_iterator iit = toRender.begin(); iit != toRender.end(); iit++)
				{
					if(desttile)
						BlitItem(draw_x, draw_y, desttile, *iit, true, 160,160,160,160);
					else
						BlitItem(draw_x, draw_y, pos, *iit, true, 160,160,160,160);
				}

				if(tile->creature && tile->creature->isSelected() && options.show_creatures)
					BlitCreature(draw_x, draw_y, tile->creature);
				if(tile->spawn && tile->spawn->isSelected())
					BlitSpriteType(draw_x, draw_y, SPRITE_SPAWN, 160, 160, 160, 160);
			}
		}
	}

	glDisable(GL_TEXTURE_2D);
}

void MapDrawer::DrawHigherFloors()
{
	glEnable(GL_TEXTURE_2D);

	// Draw "transparent higher floor"
	if(floor != 8 && floor != 0 && options.transparent_floors)
	{
		int map_z = floor - 1;
		for(int map_x = start_x; map_x <= end_x; map_x++)
		{
			for(int map_y = start_y; map_y <= end_y; map_y++)
			{
				Tile* tile = editor.map.getTile(map_x, map_y, map_z);
				if(tile)
				{
					int draw_x = map_x*32 - view_scroll_x; if(map_z <= 7) draw_x -= (7-map_z)*32; else draw_x -= 32*(floor - map_z);
					int draw_y = map_y*32 - view_scroll_y; if(map_z <= 7) draw_y -= (7-map_z)*32; else draw_y -= 32*(floor - map_z);
					//Position pos = tile->getPosition();

					if(tile->ground) {
						if(tile->isPZ()) {
							BlitItem(draw_x, draw_y, tile, tile->ground, false, 128,255,128, 96);
						} else {
							BlitItem(draw_x, draw_y, tile, tile->ground, false, 255,255,255, 96);
						}
					}
					if(zoom <= 10.0 || options.hide_items_when_zoomed == false)
					{
						ItemVector::iterator it;
						for(it = tile->items.begin(); it != tile->items.end(); it++)
							BlitItem(draw_x, draw_y, tile, *it, false, 255,255,255, 96);
					}
				}
			}
		}
	}

	glDisable(GL_TEXTURE_2D);
}

void MapDrawer::DrawSelectionBox()
{
	if(options.ingame)
		return;

	// Draw bounding box

	int last_click_rx = canvas->last_click_abs_x - view_scroll_x;
	int last_click_ry = canvas->last_click_abs_y - view_scroll_y;
	double cursor_rx = canvas->cursor_x * zoom;
	double cursor_ry = canvas->cursor_y * zoom;
	
	double lines[4][4];

	lines[0][0] = last_click_rx;
	lines[0][1] = last_click_ry;
	lines[0][2] = cursor_rx;
	lines[0][3] = last_click_ry;

	lines[1][0] = cursor_rx;
	lines[1][1] = last_click_ry;
	lines[1][2] = cursor_rx;
	lines[1][3] = cursor_ry;

	lines[2][0] = cursor_rx;
	lines[2][1] = cursor_ry;
	lines[2][2] = last_click_rx;
	lines[2][3] = cursor_ry;

	lines[3][0] = last_click_rx;
	lines[3][1] = cursor_ry;
	lines[3][2] = last_click_rx;
	lines[3][3] = last_click_ry;

	glEnable(GL_LINE_STIPPLE);
	glLineStipple(1, 0xf0);
	glLineWidth(1.0);
	glColor4f(1.0,1.0,1.0,1.0);
	glBegin(GL_LINES);
	for(int i = 0; i < 4; i++) {
		glVertex2f(lines[i][0], lines[i][1]);
		glVertex2f(lines[i][2], lines[i][3]);
	}
	glEnd();
	glDisable(GL_LINE_STIPPLE);
}

void MapDrawer::DrawLiveCursors()
{
	if (options.ingame) {
		return;
	}

	if (!editor.IsLive()) {
		return;
	}

	LiveSocket& live = editor.GetLive();
	for (LiveCursor& cursor : live.getCursorList()) {
		if (cursor.pos.z <= 7 && floor > 7) {
			continue;
		}

		if (cursor.pos.z > 7 && floor <= 8) {
			continue;
		}

		if (cursor.pos.z < floor) {
			cursor.color = wxColor(
				cursor.color.Red(),
				cursor.color.Green(),
				cursor.color.Blue(),
				std::max<uint8_t>(cursor.color.Alpha() / 2, 64)
			);
		}

		float draw_x = (cursor.pos.x * 32) - view_scroll_x;
		if (cursor.pos.z <= 7) {
			draw_x -= (7 - cursor.pos.z) * 32;
		} else {
			draw_x -= 32 * (floor - cursor.pos.z);
		}

		float draw_y = (cursor.pos.y * 32) - view_scroll_y;
		if (cursor.pos.z <= 7) {
			draw_y -= (7 - cursor.pos.z) * 32;
		} else {
			draw_y -= 32 * (floor - cursor.pos.z);
		}

		glColor(cursor.color);
		glBegin(GL_QUADS);
			glVertex2f(draw_x, draw_y);
			glVertex2f(draw_x + 32, draw_y);
			glVertex2f(draw_x + 32, draw_y + 32);
			glVertex2f(draw_x, draw_y + 32);
		glEnd();
	}
}

void MapDrawer::DrawBrush()
{
	if(!gui.IsDrawingMode())
		return;
	if(!gui.GetCurrentBrush())
		return;
	if(options.ingame)
		return;

	// This is SO NOT A GOOD WAY TO DO THINGS
	Brush* brush = gui.GetCurrentBrush();
	RAWBrush* rawbrush = dynamic_cast<RAWBrush*>(brush);
	TerrainBrush* terrainbrush = dynamic_cast<TerrainBrush*>(brush);
	WallBrush* wall_brush = dynamic_cast<WallBrush*>(brush);
	TableBrush* table_brush = dynamic_cast<TableBrush*>(brush);
	CarpetBrush* carpet_brush = dynamic_cast<CarpetBrush*>(brush);
	DoorBrush* door_brush = dynamic_cast<DoorBrush*>(brush);
	OptionalBorderBrush* optional_brush = dynamic_cast<OptionalBorderBrush*>(brush);
	CreatureBrush* creature_brush = dynamic_cast<CreatureBrush*>(brush);
	SpawnBrush* spawn_brush = dynamic_cast<SpawnBrush*>(brush);
	HouseBrush* house_brush = dynamic_cast<HouseBrush*>(brush);
	HouseExitBrush* house_exit_brush = dynamic_cast<HouseExitBrush*>(brush);
	WaypointBrush* waypoint_brush = dynamic_cast<WaypointBrush*>(brush);
	FlagBrush* flag_brush = dynamic_cast<FlagBrush*>(brush);
	EraserBrush* eraser = dynamic_cast<EraserBrush*>(brush);
	
	BrushColor brushColor = COLOR_BLANK;
	
	if(terrainbrush || table_brush || carpet_brush)
		brushColor = COLOR_BRUSH;
	else if(house_brush)
		brushColor = COLOR_HOUSE_BRUSH;
	else if(flag_brush)
		brushColor = COLOR_FLAG_BRUSH;
	else if(spawn_brush)
		brushColor = COLOR_SPAWN_BRUSH;
	else if(eraser)
		brushColor = COLOR_ERASER;

	if(dragging_draw)
	{
		ASSERT(brush->canDrag());

		if(wall_brush)
		{
			int last_click_start_map_x = std::min(canvas->last_click_map_x, mouse_map_x);
			int last_click_start_map_y = std::min(canvas->last_click_map_y, mouse_map_y);
			int last_click_end_map_x   = std::max(canvas->last_click_map_x, mouse_map_x)+1;
			int last_click_end_map_y   = std::max(canvas->last_click_map_y, mouse_map_y)+1;

			int last_click_start_sx = last_click_start_map_x*32 - view_scroll_x - getFloorAdjustment(floor);
			int last_click_start_sy = last_click_start_map_y*32 - view_scroll_y - getFloorAdjustment(floor);
			int last_click_end_sx   = last_click_end_map_x*32 - view_scroll_x - getFloorAdjustment(floor);
			int last_click_end_sy   = last_click_end_map_y*32 - view_scroll_y - getFloorAdjustment(floor);

			int delta_x = last_click_end_sx - last_click_start_sx;
			int delta_y = last_click_end_sy - last_click_start_sy;

			glColor(brushColor);
			glBegin(GL_QUADS);
				{
					glVertex2f(last_click_start_sx,    last_click_start_sy+32);
					glVertex2f(last_click_end_sx,      last_click_start_sy+32);
					glVertex2f(last_click_end_sx,      last_click_start_sy);
					glVertex2f(last_click_start_sx,    last_click_start_sy);
				}

				if(delta_y > 32)
				{
					glVertex2f(last_click_start_sx,    last_click_end_sy-32);
					glVertex2f(last_click_start_sx+32, last_click_end_sy-32);
					glVertex2f(last_click_start_sx+32, last_click_start_sy+32);
					glVertex2f(last_click_start_sx,    last_click_start_sy+32);
				}

				if(delta_x > 32 && delta_y > 32)
				{
					glVertex2f(last_click_end_sx-32,   last_click_start_sy+32);
					glVertex2f(last_click_end_sx,      last_click_start_sy+32);
					glVertex2f(last_click_end_sx,      last_click_end_sy-32);
					glVertex2f(last_click_end_sx-32,   last_click_end_sy-32);
				}

				if(delta_y > 32)
				{
					glVertex2f(last_click_start_sx,    last_click_end_sy-32);
					glVertex2f(last_click_end_sx,      last_click_end_sy-32);
					glVertex2f(last_click_end_sx,      last_click_end_sy);
					glVertex2f(last_click_start_sx,    last_click_end_sy);
				}
			glEnd();
		}
		else
		{
			if(rawbrush)
				glEnable(GL_TEXTURE_2D);

			if(gui.GetBrushShape() == BRUSHSHAPE_SQUARE || spawn_brush /* Spawn brush is always square */)
			{
				if(rawbrush || optional_brush)
				{
					int start_x, end_x;
					int start_y, end_y;

					if(mouse_map_x < canvas->last_click_map_x)
					{
						start_x = mouse_map_x;
						end_x = canvas->last_click_map_x;
					}
					else
					{
						start_x = canvas->last_click_map_x;
						end_x = mouse_map_x;
					}
					if(mouse_map_y < canvas->last_click_map_y)
					{
						start_y = mouse_map_y;
						end_y = canvas->last_click_map_y;
					}
					else
					{
						start_y = canvas->last_click_map_y;
						end_y = mouse_map_y;
					}

					for(int y = start_y; y <= end_y; y++)
					{
						int cy = y*32-view_scroll_y - getFloorAdjustment(floor);
						for(int x = start_x; x <= end_x; x++)
						{
							int cx = x*32-view_scroll_x - getFloorAdjustment(floor);
							if(optional_brush)
								glColorCheck(brush, Position(x, y, floor));
							else
								BlitSpriteType(cx, cy, rawbrush->getItemType()->sprite, 160, 160, 160, 160);
						}
					}
				}
				else
				{
					int last_click_start_map_x = std::min(canvas->last_click_map_x, mouse_map_x);
					int last_click_start_map_y = std::min(canvas->last_click_map_y, mouse_map_y);
					int last_click_end_map_x   = std::max(canvas->last_click_map_x, mouse_map_x)+1;
					int last_click_end_map_y   = std::max(canvas->last_click_map_y, mouse_map_y)+1;

					int last_click_start_sx = last_click_start_map_x*32 - view_scroll_x - getFloorAdjustment(floor);
					int last_click_start_sy = last_click_start_map_y*32 - view_scroll_y - getFloorAdjustment(floor);
					int last_click_end_sx   = last_click_end_map_x*32 - view_scroll_x - getFloorAdjustment(floor);
					int last_click_end_sy   = last_click_end_map_y*32 - view_scroll_y - getFloorAdjustment(floor);

					glColor(brushColor);
					glBegin(GL_QUADS);
						glVertex2f(last_click_start_sx,    last_click_start_sy);
						glVertex2f(last_click_end_sx,      last_click_start_sy);
						glVertex2f(last_click_end_sx,      last_click_end_sy);
						glVertex2f(last_click_start_sx,    last_click_end_sy);
					glEnd();
				}

			}
			else if(gui.GetBrushShape() == BRUSHSHAPE_CIRCLE)
			{
				// Calculate drawing offsets
				int start_x, end_x;
				int start_y, end_y;
				int width = std::max(
					std::abs(std::max(mouse_map_y, canvas->last_click_map_y) - std::min(mouse_map_y, canvas->last_click_map_y)),
					std::abs(std::max(mouse_map_x, canvas->last_click_map_x) - std::min(mouse_map_x, canvas->last_click_map_x))
					);

				if(mouse_map_x < canvas->last_click_map_x)
				{
					start_x = canvas->last_click_map_x - width;
					end_x = canvas->last_click_map_x;
				}
				else
				{
					start_x = canvas->last_click_map_x;
					end_x = canvas->last_click_map_x + width;
				}

				if(mouse_map_y < canvas->last_click_map_y)
				{
					start_y = canvas->last_click_map_y - width;
					end_y = canvas->last_click_map_y;
				}
				else
				{
					start_y = canvas->last_click_map_y;
					end_y = canvas->last_click_map_y + width;
				}

				int center_x = start_x + (end_x - start_x) / 2;
				int center_y = start_y + (end_y - start_y) / 2;
				float radii = width / 2.0f + 0.005f;

				for(int y = start_y-1; y <= end_y+1; y++)
				{
					int cy = y*32-view_scroll_y - getFloorAdjustment(floor);
					float dy = center_y - y;
					for(int x = start_x-1; x <= end_x+1; x++)
					{
						int cx = x*32-view_scroll_x - getFloorAdjustment(floor);

						float dx = center_x - x;
						//printf("%f;%f\n", dx, dy);
						float distance = sqrt(dx*dx + dy*dy);
						if(distance < radii)
						{
							if(rawbrush)
								BlitSpriteType(cx, cy, rawbrush->getItemType()->sprite, 160, 160, 160, 160);
							else
							{
								glColor(brushColor);
								glBegin(GL_QUADS);
									glVertex2f(cx   ,cy+32);
									glVertex2f(cx+32,cy+32);
									glVertex2f(cx+32,cy);
									glVertex2f(cx,   cy);
								glEnd();
							}
						}
					}
				}
			}

			if(rawbrush)
				glDisable(GL_TEXTURE_2D);
		}
	}
	else
	{
		if(wall_brush)
		{
			int start_map_x = mouse_map_x - gui.GetBrushSize();
			int start_map_y = mouse_map_y - gui.GetBrushSize();
			int end_map_x   = mouse_map_x + gui.GetBrushSize() + 1;
			int end_map_y   = mouse_map_y + gui.GetBrushSize() + 1;

			int start_sx = start_map_x*32 - view_scroll_x - getFloorAdjustment(floor);
			int start_sy = start_map_y*32 - view_scroll_y - getFloorAdjustment(floor);
			int end_sx   = end_map_x*32 - view_scroll_x - getFloorAdjustment(floor);
			int end_sy   = end_map_y*32 - view_scroll_y - getFloorAdjustment(floor);

			int delta_x = end_sx - start_sx;
			int delta_y = end_sy - start_sy;
			
			glColor(brushColor);
			glBegin(GL_QUADS);
				{
					glVertex2f(start_sx,    start_sy+32);
					glVertex2f(end_sx,      start_sy+32);
					glVertex2f(end_sx,      start_sy);
					glVertex2f(start_sx,    start_sy);
				}

				if(delta_y > 32)
				{
					glVertex2f(start_sx,    end_sy-32);
					glVertex2f(start_sx+32, end_sy-32);
					glVertex2f(start_sx+32, start_sy+32);
					glVertex2f(start_sx,    start_sy+32);
				}

				if(delta_x > 32 && delta_y > 32)
				{
					glVertex2f(end_sx-32,   start_sy+32);
					glVertex2f(end_sx,      start_sy+32);
					glVertex2f(end_sx,      end_sy-32);
					glVertex2f(end_sx-32,   end_sy-32);
				}

				if(delta_y > 32)
				{
					glVertex2f(start_sx,    end_sy-32);
					glVertex2f(end_sx,      end_sy-32);
					glVertex2f(end_sx,      end_sy);
					glVertex2f(start_sx,    end_sy);
				}
			glEnd();
		}
		else if(door_brush)
		{
			int cx = (mouse_map_x)*32-view_scroll_x - getFloorAdjustment(floor);
			int cy = (mouse_map_y)*32-view_scroll_y - getFloorAdjustment(floor);

			glColorCheck(brush, Position(mouse_map_x, mouse_map_y, floor));
			glBegin(GL_QUADS);
				glVertex2f(cx   ,cy+32);
				glVertex2f(cx+32,cy+32);
				glVertex2f(cx+32,cy);
				glVertex2f(cx,   cy);
			glEnd();
		}
		else if(creature_brush)
		{
			glEnable(GL_TEXTURE_2D);
			int cy = (mouse_map_y)*32-view_scroll_y - getFloorAdjustment(floor);
			int cx = (mouse_map_x)*32-view_scroll_x - getFloorAdjustment(floor);
			if(creature_brush->canDraw(&editor.map, Position(mouse_map_x, mouse_map_y, floor)))
				BlitCreature(cx, cy, creature_brush->getType()->outfit, SOUTH, 255, 255, 255, 160);
			else
				BlitCreature(cx, cy, creature_brush->getType()->outfit, SOUTH, 255, 64, 64, 160);
			
			glDisable(GL_TEXTURE_2D);
		}
		else if(!dynamic_cast<DoodadBrush*>(brush))
		{
			if(rawbrush)
			{ // Textured brush
				glEnable(GL_TEXTURE_2D);
			}
			for(int y = -gui.GetBrushSize()-1; y <= gui.GetBrushSize()+1; y++)
			{
				int cy = (mouse_map_y+y)*32-view_scroll_y - getFloorAdjustment(floor);
				for(int x = -gui.GetBrushSize()-1; x <= gui.GetBrushSize()+1; x++)
				{
					int cx = (mouse_map_x+x)*32-view_scroll_x - getFloorAdjustment(floor);
					if(gui.GetBrushShape() == BRUSHSHAPE_SQUARE)
					{
						if(
								x >= -gui.GetBrushSize() &&
								x <= gui.GetBrushSize() &&
								y >= -gui.GetBrushSize() &&
								y <= gui.GetBrushSize()
							)
						{
							if(rawbrush)
							{
								BlitSpriteType(cx, cy, rawbrush->getItemType()->sprite, 160, 160, 160, 160);
							}
							else
							{
								if(waypoint_brush || house_exit_brush || optional_brush)
									glColorCheck(brush, Position(mouse_map_x + x, mouse_map_y + y, floor));
								else
									glColor(brushColor);

								glBegin(GL_QUADS);
									glVertex2f(cx   ,cy+32);
									glVertex2f(cx+32,cy+32);
									glVertex2f(cx+32,cy);
									glVertex2f(cx,   cy);
								glEnd();
							}
						}
					}
					else if(gui.GetBrushShape() == BRUSHSHAPE_CIRCLE)
					{
						double distance = sqrt(double(x*x) + double(y*y));
						if(distance < gui.GetBrushSize()+0.005)
						{
							if(rawbrush)
							{
								BlitSpriteType(cx, cy, rawbrush->getItemType()->sprite, 160, 160, 160, 160);
							}
							else
							{
								if(waypoint_brush || house_exit_brush || optional_brush)
									glColorCheck(brush, Position(mouse_map_x + x, mouse_map_y + y, floor));
								else
									glColor(brushColor);

								glBegin(GL_QUADS);
									glVertex2f(cx   ,cy+32);
									glVertex2f(cx+32,cy+32);
									glVertex2f(cx+32,cy);
									glVertex2f(cx,   cy);
								glEnd();
							}
						}
					}
				}
			}
		}
	}

	if(rawbrush) { // Textured brush
		glDisable(GL_TEXTURE_2D);
	}
}

void MapDrawer::BlitItem(int& draw_x, int& draw_y, const Tile* tile, const Item* item, bool ephemeral, int red, int green, int blue, int alpha) {
	ItemType& it = item_db[item->getID()];

	if(!options.ingame && !ephemeral && item->isSelected()) {
		red /= 2;
		blue /= 2;
		green /= 2;
	}

	// Ugly hacks. :)
	if(it.id == 0) {
		glDisable(GL_TEXTURE_2D);
		glBlitSquare(draw_x, draw_y, 255, 0, 0, alpha);
		glEnable(GL_TEXTURE_2D);
		return;
	} else if(it.id == 459 && !options.ingame) { 
		glDisable(GL_TEXTURE_2D);
		glBlitSquare(draw_x, draw_y, red, green, 0, alpha/3*2);
		glEnable(GL_TEXTURE_2D);
		return;
	} else if(it.id == 460 && !options.ingame) {
		glDisable(GL_TEXTURE_2D);
		glBlitSquare(draw_x, draw_y, red, 0, 0, alpha/3*2);
		glEnable(GL_TEXTURE_2D);
		return;
	}


	GameSprite* spr = it.sprite;

	if(it.isMetaItem())
		return;
	if(spr == nullptr)
		return;
	if(!ephemeral && it.pickupable && options.show_items == false)
		return;

	int screenx = draw_x - spr->getDrawOffset().first;
	int screeny = draw_y - spr->getDrawOffset().second;

	const Position& pos = tile->getPosition();

	// Set the newd drawing height accordingly
	draw_x -= spr->getDrawHeight();
	draw_y -= spr->getDrawHeight();

	int subtype = -1;

	int pattern_x = pos.x % spr->pattern_x;
	int pattern_y = pos.y % spr->pattern_y;
	int pattern_z = pos.z % spr->pattern_z;

	if(it.isSplash() || it.isFluidContainer()) {
		subtype = item->getSubtype();
	} else if(it.isHangable) {
		if(tile->hasProperty(ISVERTICAL)) {
			pattern_x = 2;
		} else if(tile->hasProperty(ISHORIZONTAL)) {
			pattern_x = 1;
		} else {
			pattern_x = -0;
		}
	} else if(it.stackable) {
		if(item->getSubtype() <= 1)
			subtype = 0;
		else if(item->getSubtype() <= 2)
			subtype = 1;
		else if(item->getSubtype() <= 3)
			subtype = 2;
		else if(item->getSubtype() <= 4)
			subtype = 3;
		else if(item->getSubtype() < 10)
			subtype = 4;
		else if(item->getSubtype() < 25)
			subtype = 5;
		else if(item->getSubtype() < 50)
			subtype = 6;
		else
			subtype = 7;
	}

	if(!ephemeral && options.transparent_items &&
			(!it.isGroundTile() || spr->width > 1 || spr->height > 1) &&
			!it.isSplash() &&
			(!it.isBorder || spr->width > 1 || spr->height > 1)
	  )
	{
		alpha /= 2;
	}

	int tme = 0; //GetTime() % itype->FPA;
	for(int cx = 0; cx != spr->width; cx++) {
		for(int cy = 0; cy != spr->height; cy++) {
			for(int cf = 0; cf != spr->layers; cf++) {
				int texnum = spr->getHardwareID(cx,cy,cf,
					subtype,
					pattern_x,
					pattern_y,
					pattern_z,
					tme
				);
				glBlitTexture(screenx-cx*32, screeny-cy*32, texnum, red, green, blue, alpha);
			}
		}
	}
}

void MapDrawer::BlitItem(int& draw_x, int& draw_y, const Position& pos, const Item* item, bool ephemeral, int red, int green, int blue, int alpha) {
	ItemType& it = item_db[item->getID()];

	if(!options.ingame && !ephemeral && item->isSelected()) {
		red /= 2;
		blue /= 2;
		green /= 2;
	}

	if(it.id == 459 && !options.ingame) { // Ugly hack yes?
		glDisable(GL_TEXTURE_2D);
		glBlitSquare(draw_x, draw_y, red, green, 0, alpha/3*2);
		glEnable(GL_TEXTURE_2D);
		return;
	} else if(it.id == 460 && !options.ingame) { // Ugly hack yes?
		glDisable(GL_TEXTURE_2D);
		glBlitSquare(draw_x, draw_y, red, 0, 0, alpha/3*2);
		glEnable(GL_TEXTURE_2D);
		return;
	}

	GameSprite* spr = it.sprite;

	if(it.isMetaItem())
		return;
	if(spr == nullptr)
		return;
	if(!ephemeral && it.pickupable && options.show_items)
		return;

	int screenx = draw_x - spr->getDrawOffset().first;
	int screeny = draw_y - spr->getDrawOffset().second;

	// Set the newd drawing height accordingly
	draw_x -= spr->getDrawHeight();
	draw_y -= spr->getDrawHeight();

	int subtype = -1;

	int pattern_x = pos.x % spr->pattern_x;
	int pattern_y = pos.y % spr->pattern_y;
	int pattern_z = pos.z % spr->pattern_z;

	if(it.isSplash() || it.isFluidContainer()) {
		subtype = item->getSubtype();
	} else if(it.isHangable) {
		pattern_x = 0;
		/*
		if(tile->hasProperty(ISVERTICAL)) {
			pattern_x = 2;
		} else if(tile->hasProperty(ISHORIZONTAL)) {
			pattern_x = 1;
		} else {
			pattern_x = -0;
		}
		*/
	} else if(it.stackable) {
		if(item->getSubtype() <= 1)
			subtype = 0;
		else if(item->getSubtype() <= 2)
			subtype = 1;
		else if(item->getSubtype() <= 3)
			subtype = 2;
		else if(item->getSubtype() <= 4)
			subtype = 3;
		else if(item->getSubtype() < 10)
			subtype = 4;
		else if(item->getSubtype() < 25)
			subtype = 5;
		else if(item->getSubtype() < 50)
			subtype = 6;
		else
			subtype = 7;
	}

	if(!ephemeral && options.transparent_items &&
			(!it.isGroundTile() || spr->width > 1 || spr->height > 1) &&
			!it.isSplash() &&
			(!it.isBorder || spr->width > 1 || spr->height > 1)
	  )
	{
		alpha /= 2;
	}

	int tme = 0; //GetTime() % itype->FPA;
	for(int cx = 0; cx != spr->width; ++cx) {
		for(int cy = 0; cy != spr->height; ++cy) {
			for(int cf = 0; cf != spr->layers; ++cf) {
				int texnum = spr->getHardwareID(cx,cy,cf,
					subtype,
					pattern_x,
					pattern_y,
					pattern_z,
					tme
				);
				glBlitTexture(screenx-cx*32, screeny-cy*32, texnum, red, green, blue, alpha);
			}
		}
	}
}

void MapDrawer::BlitSpriteType(int screenx, int screeny, uint32_t spriteid, int red, int green, int blue, int alpha) {
	GameSprite* spr = item_db[spriteid].sprite;
	if(spr == nullptr) return;
	screenx -= spr->getDrawOffset().first;
	screeny -= spr->getDrawOffset().second;

	int tme = 0; //GetTime() % itype->FPA;
	for(int cx = 0; cx != spr->width; ++cx) {
		for(int cy = 0; cy != spr->height; ++cy) {
			for(int cf = 0; cf != spr->layers; ++cf) {
				int texnum = spr->getHardwareID(cx,cy,cf,-1,0,0,0,tme);
				//printf("CF: %d\tTexturenum: %d\n", cf, texnum);
				glBlitTexture(screenx-cx*32, screeny-cy*32, texnum, red, green, blue, alpha);
			}
		}
	}
}

void MapDrawer::BlitSpriteType(int screenx, int screeny, GameSprite* spr, int red, int green, int blue, int alpha) {
	if(spr == nullptr) return;
	screenx -= spr->getDrawOffset().first;
	screeny -= spr->getDrawOffset().second;

	int tme = 0; //GetTime() % itype->FPA;
	for(int cx = 0; cx != spr->width; ++cx) {
		for(int cy = 0; cy != spr->height; ++cy) {
			for(int cf = 0; cf != spr->layers; ++cf) {
				int texnum = spr->getHardwareID(cx,cy,cf,-1,0,0,0,tme);
				//printf("CF: %d\tTexturenum: %d\n", cf, texnum);
				glBlitTexture(screenx-cx*32, screeny-cy*32, texnum, red, green, blue, alpha);
			}
		}
	}
}

void MapDrawer::BlitCreature(int screenx, int screeny, const Outfit& outfit, Direction dir, int red, int green, int blue, int alpha)
{
	if(outfit.lookItem != 0)
	{
		ItemType& it = item_db[outfit.lookItem];
		BlitSpriteType(screenx, screeny, it.sprite, red, green, blue, alpha);
	}
	else
	{
		GameSprite* spr = gui.gfx.getCreatureSprite(outfit.lookType);
		if(!spr || outfit.lookType == 0)
		{
			return;
			/*
			spr = gui.gfx.getCreatureSprite(138);
			if (!spr)
				return;
			 */
		}

		int tme = 0; //GetTime() % itype->FPA;
		for(int cx = 0; cx != spr->width; ++cx) {
			for(int cy = 0; cy != spr->height; ++cy) {
				int texnum = spr->getHardwareID(cx,cy,(int)dir,outfit,tme);
				glBlitTexture(screenx-cx*32, screeny-cy*32, texnum, red, green, blue, alpha);
			}
		}
	}
}

void MapDrawer::BlitCreature(int screenx, int screeny, const Creature* c, int red, int green, int blue, int alpha)
{
	if(!options.ingame && c->isSelected())
	{
		red /= 2;
		green /= 2;
		blue /= 2;
	}
	BlitCreature(screenx, screeny, c->getLookType(), c->getDirection(), red, green, blue, alpha);
}

void MapDrawer::MakeTooltip(Item* item, std::ostringstream& tip)
{
	if(item->getID() > 100)
		tip << "id: " << item->getID() << "\n";
	if(item->getUniqueID() > 0)
		tip << "uid: " << item->getUniqueID() << "\n";
	if(item->getActionID() > 0)
		tip << "aid: " << item->getActionID() << "\n";
	if(item->getText() != "")
		tip << "text: " << item->getText() << "\n";
}

void MapDrawer::DrawTile(TileLocation* location) {
	if(!location)
		return;
	Tile* tile = location->get();

	if(!tile)
		return;

	if(options.show_only_modified && !tile->isModified())
		return;

	int map_x = location->getX();
	int map_y = location->getY();
	int map_z = location->getZ();

	//std::ostringstream tooltip;

	bool only_colors = options.show_only_colors;

	int draw_x = map_x*32 - view_scroll_x;
	if(map_z <= 7)
		draw_x -= (7-map_z)*32;
	else
		draw_x -= 32*(floor-map_z);

	int draw_y = map_y*32 - view_scroll_y;
	if(map_z <= 7)
		draw_y -= (7-map_z)*32;
	else
		draw_y -= 32*(floor-map_z);

	uint8_t r = 255,g = 255,b = 255;
	if(tile->ground || only_colors)
	{
		bool showspecial = options.show_only_colors || options.show_special_tiles;
		
		if(tile->isBlocking() && tile->size() > 0 && options.show_blocking)
		{
			g = g/3*2;
			b = b/3*2;
		}
		
		int item_count = tile->items.size();
		if (options.highlight_items && item_count > 0 && tile->items.back()->isBorder() == false)
		{
			static const float factor[5] = {0.75f, 0.6f, 0.48f, 0.40f, 0.33f};
			int idx = (item_count < 5 ? item_count : 5) - 1;
			g = int(g * factor[idx]);
			r = int(r * factor[idx]);
		}

		if(location->getSpawnCount() > 0 && options.show_spawns)
		{
			float f = 1.0f;
			for(uint32_t i = 0; i < location->getSpawnCount(); ++i)
			{
				f *= 0.7f;
			}
			g = uint8_t(g * f);
			b = uint8_t(b * f);
		}

		if(tile->isHouseTile() && options.show_houses)
		{
			if((int)tile->getHouseID() == current_house_id)
			{
				r /= 2;
			}
			else
			{
				r /= 2;
				g /= 2;
			}
		}
		else if(showspecial && tile->isPZ())
		{
			r /= 2;
			b /= 2;
		}

		if(showspecial && tile->getMapFlags() & TILESTATE_PVPZONE)
		{
			g = r/4;
			b = b/3*2;
		}

		if(showspecial && tile->getMapFlags() & TILESTATE_NOLOGOUT)
		{
			b /= 2;
		}

		if(showspecial && tile->getMapFlags() & TILESTATE_NOPVP)
		{
			g /= 2;
		}

		if(only_colors)
		{
			if(r != 255 || g != 255 || b != 255)
			{
				glBlitSquare(draw_x, draw_y, r, g, b, 128);
			}
		}
		else
		{
			BlitItem(draw_x, draw_y, tile, tile->ground, false, r, g, b);
		}
		//MakeTooltip(tile->ground, tooltip);
	}

	if(only_colors == false)
	{
		if(zoom < 10.0 || options.hide_items_when_zoomed == false)
		{
			for(ItemVector::iterator it = tile->items.begin(); it != tile->items.end(); it++)
			{
				//MakeTooltip(*it, tooltip);
				if((*it)->isBorder())
				{
					BlitItem(draw_x, draw_y, tile, *it, false, r, g, b);
				}
				else
				{
					BlitItem(draw_x, draw_y, tile, *it);
				}
			}
			if(tile->creature && options.show_creatures)
			{
				BlitCreature(draw_x, draw_y, tile->creature);
			}
		}
		if(location->getWaypointCount() > 0 && options.show_houses)
		{
			BlitSpriteType(draw_x, draw_y, SPRITE_FLAME_BLUE, 64, 64, 255);
		}

		if(tile->isHouseExit() && options.show_houses)
		{
			if(tile->hasHouseExit(current_house_id))
			{
				BlitSpriteType(draw_x, draw_y, SPRITE_FLAG_GREY, 64, 255, 255);
			}
			else
			{
				BlitSpriteType(draw_x, draw_y, SPRITE_FLAG_GREY, 64, 64, 255);
			}
		}
		//if(tile->isTownExit()) {
		//	BlitSpriteType(draw_x, draw_y, SPRITE_FLAG_GREY, 255, 255, 64);
		//}
		if(tile->spawn && options.show_spawns)
		{
			if(tile->spawn->isSelected())
			{
				BlitSpriteType(draw_x, draw_y, SPRITE_SPAWN, 128, 128, 128);
			}
			else
			{
				BlitSpriteType(draw_x, draw_y, SPRITE_SPAWN, 255, 255, 255);
			}
		}
	}

	//
	//DrawTooltip(draw_x, draw_y, tooltip.str());
}

void MapDrawer::DrawTooltips()
{
	for(std::vector<MapTooltip>::const_iterator tooltip = tooltips.begin(); tooltip != tooltips.end(); ++tooltip)
	{
		wxCoord width, height;
		wxCoord lineHeight;
		pdc.GetMultiLineTextExtent(wxstr(tooltip->tip), &width, &height, &lineHeight);

		int start_sx = tooltip->x + 16 - width / 2;
		int start_sy = tooltip->y + 16 - 7 - height;
		int end_sx = tooltip->x + 16 + width / 2;
		int end_sy = tooltip->y + 16 - 7;


		int vertexes [9][2] = {
			{tooltip->x,  start_sy},
			{end_sx,      start_sy},
			{end_sx,      end_sy},
			{tooltip->x + 23, end_sy},
			{tooltip->x + 16, end_sy+7},
			{tooltip->x +  9, end_sy},
			{start_sx,    end_sy},
			{start_sx,    start_sy},
			{tooltip->x,  start_sy}
		};

		glColor4ub(255, 255, 225, 255);
		glBegin(GL_POLYGON);
		for(int i = 0; i < 8; ++i)
			glVertex2i(vertexes[i][0], vertexes[i][1]);
		glEnd();
		
		glColor4ub(0, 0, 0, 255);
		glLineWidth(1.0);
		glBegin(GL_LINES);
		for(int i = 0; i < 8; ++i)
		{
			glVertex2i(vertexes[i  ][0], vertexes[i  ][1]);
			glVertex2i(vertexes[i+1][0], vertexes[i+1][1]);
		}
		glEnd();
	}
}

void MapDrawer::DrawTooltip(int screenx, int screeny, const std::string& s)
{
	if(s.empty())
		return;

	MapTooltip tooltip;
	tooltip.x = screenx;
	tooltip.y = screeny;
	tooltip.tip = s;

	if(tooltip.tip.at(tooltip.tip.size() - 1) == '\n')
		tooltip.tip.resize(tooltip.tip.size() - 1);
	tooltips.push_back(tooltip);
}

void MapDrawer::TakeScreenshot(uint8_t* screenshot_buffer)
{
	glFinish(); // Wait for the operation to finish

	glPixelStorei(GL_PACK_ALIGNMENT, 1); // 1 byte alignment

	for(int i = 0; i < screensize_y; ++i)
		glReadPixels(0, screensize_y - i, screensize_x, 1, GL_RGB, GL_UNSIGNED_BYTE, (GLubyte*)(screenshot_buffer) + 3*screensize_x*i);
}


void MapDrawer::glBlitTexture(int sx, int sy, int texture_number, int red, int green, int blue, int alpha) {
	if(texture_number != 0) {
		glBindTexture(GL_TEXTURE_2D, texture_number);
		glColor4ub(uint8_t(red), uint8_t(green), uint8_t(blue), uint8_t(alpha));
		glBegin(GL_QUADS);
			glTexCoord2f(0.f, 0.f); glVertex2f(sx,   sy);
			glTexCoord2f(1.f, 0.f); glVertex2f(sx+32,sy);
			glTexCoord2f(1.f, 1.f); glVertex2f(sx+32,sy+32);
			glTexCoord2f(0.f, 1.f); glVertex2f(sx,   sy+32);
		glEnd();
	}
}

void MapDrawer::glBlitSquare(int sx, int sy, int red, int green, int blue, int alpha)
{
	glColor4ub(uint8_t(red), uint8_t(green), uint8_t(blue), uint8_t(alpha));
	glBegin(GL_QUADS);
		glVertex2f(sx,   sy);
		glVertex2f(sx+32,sy);
		glVertex2f(sx+32,sy+32);
		glVertex2f(sx,   sy+32);
	glEnd();
}

void MapDrawer::glColor(wxColor color)
{
	glColor4ub(color.Red(), color.Green(), color.Blue(), color.Alpha());
}

void MapDrawer::glColor(MapDrawer::BrushColor color)
{
	switch(color)
	{
	case COLOR_BRUSH:
		glColor4ub(
			settings.getInteger(Config::CURSOR_RED),
			settings.getInteger(Config::CURSOR_GREEN),
			settings.getInteger(Config::CURSOR_BLUE),
			settings.getInteger(Config::CURSOR_ALPHA)
		);
		break;
	case COLOR_FLAG_BRUSH:
	case COLOR_HOUSE_BRUSH:
		glColor4ub(
			settings.getInteger(Config::CURSOR_ALT_RED),
			settings.getInteger(Config::CURSOR_ALT_GREEN),
			settings.getInteger(Config::CURSOR_ALT_BLUE),
			settings.getInteger(Config::CURSOR_ALT_ALPHA)
		);
		break;
	case COLOR_SPAWN_BRUSH:
		glColor4ub(166, 0, 0, 128);
		break;
	case COLOR_ERASER:
		glColor4ub(166, 0, 0, 128);
		break;
	case COLOR_VALID:
		glColor4ub(0, 166, 0, 128);
		break;
	case COLOR_INVALID:
		glColor4ub(166, 0, 0, 128);
		break;
	default:
		glColor4ub(255, 255, 255, 128);
		break;
	}
}

void MapDrawer::glColorCheck(Brush* brush, const Position& pos)
{
	if(brush->canDraw(&editor.map, pos))
		glColor(COLOR_VALID);
	else
		glColor(COLOR_INVALID);
}
