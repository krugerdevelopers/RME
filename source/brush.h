//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////
// $URL: http://svn.rebarp.se/svn/RME/trunk/source/brush.h $
// $Id: brush.h 298 2010-02-23 17:09:13Z admin $

#ifndef RME_BRUSH_H_
#define RME_BRUSH_H_
#include "main.h"

#include "position.h"

#include "brush_enums.h"

// Thanks to a million forward declarations, we don't have to include any files!
class ItemType;
class CreatureType;
class BaseMap;
class House;
class Item;
class Tile;
typedef std::vector<Tile*> TileVector;
class Brush;
class GroundBrush;
class WallBrush;
class DoorBrush;
class OptionalBorderBrush;
class RampBrush;
class HouseBrush;
class CreatureBrush;
class SpawnBrush;
class AutoBorder;

//=============================================================================
// Brushes, holds all brushes

typedef std::multimap<std::string, Brush*> BrushMap;

class Brushes {
public:
	Brushes();
	~Brushes();

	void init();
	void clear();

	Brush* getBrush(const std::string& name) const;

	void addBrush(Brush* brush);
	
	bool unserializeBorder(pugi::xml_node node, wxArrayString& warnings);
	bool unserializeBrush(pugi::xml_node node, wxArrayString& warnings);

	const BrushMap& getMap() const {return brushes;}
protected:
	typedef std::map<uint32_t, AutoBorder*> BorderMap;
	BrushMap brushes;
	BorderMap borders;

	friend class AutoBorder;
	friend class GroundBrush;
};

extern Brushes brushes;

//=============================================================================
// Common brush interface

class Brush
{
	public:
		Brush();
		virtual ~Brush();

		virtual bool load(pugi::xml_node node, wxArrayString& warnings) {
			return true;
		}

		virtual void draw(BaseMap* map, Tile* tile, void* parameter = nullptr) = 0;
		virtual void undraw(BaseMap* map, Tile* tile) = 0;
		virtual bool canDraw(BaseMap* map, const Position& position) const = 0;

		//
		uint32_t getID() const { return id; }

		virtual std::string getName() const = 0;
		virtual void setName(const std::string& newName) {
			ASSERT(_MSG("setName attempted on nameless brush!"));
		}
		
		virtual int getLookID() const = 0;
		
		virtual bool needBorders() const { return false; }

		virtual bool canDrag() const { return false; }
		virtual bool canSmear() const { return true; }

		virtual bool oneSizeFitsAll() const { return false; }

		virtual int32_t getMaxVariation() const { return 0; }

		bool visibleInPalette() const { return visible; }
		void flagAsVisible() { visible = true; }

	protected:
		static uint32_t id_counter;
		uint32_t id;
		
		bool visible; // Visible in any palette?
};

//=============================================================================
// Terrain brush interface

class TerrainBrush : public Brush
{
	public:
		TerrainBrush();
		virtual ~TerrainBrush();
	
		virtual bool canDraw(BaseMap* map, const Position& position) const { return true; }

		virtual std::string getName() const { return name; }
		virtual void setName(const std::string& newName) { name = newName; }

		virtual int32_t getZ() const { return 0; }
		virtual int32_t getLookID() const { return look_id; }

		virtual bool needBorders() const { return true; }
		virtual bool canDrag() const { return true; }
	
		bool friendOf(TerrainBrush* other);

	protected:
		std::vector<uint32_t> friends;
		std::string name;

		uint16_t look_id;
		
		bool hate_friends;
};
//=============================================================================
// FlagBrush, draw PZ etc.

class FlagBrush : public Brush {
public:
	FlagBrush(uint32_t _flag);
	virtual ~FlagBrush();

	virtual bool canDraw(BaseMap* map, const Position& position) const;
	virtual void draw(BaseMap* map, Tile* tile, void* parameter);
	virtual void undraw(BaseMap* map, Tile* tile);

	virtual bool canDrag() const {return true;}
	virtual int getLookID() const;
	virtual std::string getName() const;
protected:
	uint32_t flag;
};

//=============================================================================
// Doorbrush, add doors, windows etc.

class DoorBrush : public Brush {
public:
	DoorBrush(DoorType _doortype);
	virtual ~DoorBrush();
	
	static void switchDoor(Item* door);

	virtual bool canDraw(BaseMap* map, const Position& position) const;
	virtual void draw(BaseMap* map, Tile* tile, void* parameter);
	virtual void undraw(BaseMap* map, Tile* tile);
	
	virtual int getLookID() const;
	virtual std::string getName() const;
	virtual bool oneSizeFitsAll() const {return true;}
protected:
	DoorType doortype;
};

//=============================================================================
// OptionalBorderBrush, add gravel 'round mountains.

class OptionalBorderBrush : public Brush {
public:
	OptionalBorderBrush();
	virtual ~OptionalBorderBrush();

	virtual bool canDraw(BaseMap* map, const Position& position) const;
	virtual void draw(BaseMap* map, Tile* tile, void* parameter);
	virtual void undraw(BaseMap* map, Tile* tile);

	virtual bool canDrag() const {return true;}
	virtual int getLookID() const;
	virtual std::string getName() const;
};

//=============================================================================
// Rampbrush, add ramps to mountains.

class EraserBrush : public Brush {
public:
	EraserBrush();
	virtual ~EraserBrush();

	virtual bool canDraw(BaseMap* map, const Position& position) const;
	virtual void draw(BaseMap* map, Tile* tile, void* parameter);
	virtual void undraw(BaseMap* map, Tile* tile);
	
	virtual bool needBorders() const {return true;}
	virtual bool canDrag() const {return true;}
	virtual int getLookID() const;
	virtual std::string getName() const;
};

//=============================================================================
// Autoborder, used by GroundBrush, should be transparent to users

class AutoBorder {
public:
	AutoBorder(int _id) : id(_id), group(0), ground(false) {
		for(int i = 0; i < 13; i++) tiles[i] = 0;
	}
	~AutoBorder() {}
	
	static int edgeNameToID(const std::string& edgename);
	bool load(pugi::xml_node node, wxArrayString& warnings, GroundBrush* owner = nullptr, uint16_t ground_equivalent = 0);
	
	uint32_t tiles[13];
	uint32_t id;
	uint16_t group;
	bool ground;
};

#endif

