
#ifndef RME_MAP_DRAWER_H_
#define RME_MAP_DRAWER_H_

class GameSprite;

struct MapTooltip
{
	int x, y;
	std::string tip;
};

// Storage during drawing, for option caching
struct DrawingOptions {
	DrawingOptions();

	void SetIngame();
	void SetDefault();

	bool transparent_floors;
	bool transparent_items;
	bool show_ingame_box;
	bool ingame;
	bool dragging;

	int show_grid;
	bool show_all_floors;
	bool show_creatures;
	bool show_spawns;
	bool show_houses;
	bool show_shade;
	bool show_special_tiles;
	bool show_items;

	bool highlight_items;
	bool show_blocking;
	bool show_only_colors;
	bool show_only_modified;
	bool hide_items_when_zoomed;
};

class MapCanvas;

class MapDrawer
{
	MapCanvas* canvas;
	Editor& editor;
	wxPaintDC& pdc;
	DrawingOptions options;

	double zoom;

	uint32_t current_house_id;

	int mouse_map_x, mouse_map_y;
	int start_x, start_y, start_z;
	int end_x, end_y, end_z, superend_z;
	int view_scroll_x, view_scroll_y;
	int screensize_x, screensize_y;
	int tile_size;
	int floor;

protected:
	std::vector<MapTooltip> tooltips;

public:
	MapDrawer(const DrawingOptions& options, MapCanvas* canvas, wxPaintDC& pdc);
	~MapDrawer();

	bool dragging;
	bool dragging_draw;

	void SetupVars();
	void SetupGL();

	void Draw();
	void DrawBackground();
	void DrawMap();
	void DrawDraggingShadow();
	void DrawHigherFloors();
	void DrawSelectionBox();
	void DrawLiveCursors();
	void DrawBrush();
	void DrawIngameBox();
	void DrawGrid();
	void DrawTooltips();

	void TakeScreenshot(uint8_t* screenshot_buffer);

protected:
	void BlitItem(int& screenx, int& screeny, const Tile* tile, const Item* item, bool ephemeral = false, int red = 255, int green = 255, int blue = 255, int alpha = 255);
	void BlitItem(int& screenx, int& screeny, const Position& pos, const Item* item, bool ephemeral = false, int red = 255, int green = 255, int blue = 255, int alpha = 255);
	void BlitSpriteType(int screenx, int screeny, uint32_t spriteid, int red = 255, int green = 255, int blue = 255, int alpha = 255);
	void BlitSpriteType(int screenx, int screeny, GameSprite* spr, int red = 255, int green = 255, int blue = 255, int alpha = 255);
	void BlitCreature(int screenx, int screeny, const Creature* c, int red = 255, int green = 255, int blue = 255, int alpha = 255);
	void BlitCreature(int screenx, int screeny, const Outfit& outfit, Direction dir, int red = 255, int green = 255, int blue = 255, int alpha = 255);
	void DrawTile(TileLocation* tile);
	void DrawTooltip(int screenx, int screeny, const std::string& s);
	void MakeTooltip(Item* item, std::ostringstream& tip);

	enum BrushColor {
		COLOR_BRUSH,
		COLOR_HOUSE_BRUSH,
		COLOR_FLAG_BRUSH,
		COLOR_SPAWN_BRUSH,
		COLOR_ERASER,
		COLOR_VALID,
		COLOR_INVALID,
		COLOR_BLANK,
	};

	void glBlitTexture(int sx, int sy, int texture_number, int red, int green, int blue, int alpha);
	void glBlitSquare(int sx, int sy, int red, int green, int blue, int alpha);
	void glColor(wxColor color);
	void glColor(BrushColor color);
	void glColorCheck(Brush* brush, const Position& pos);
};


#endif

