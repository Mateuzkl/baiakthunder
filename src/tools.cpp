/**
 * The Forgotten Server - a free and open-source MMORPG server emulator
 * Copyright (C) 2019  Mark Samman <mark.samman@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "otpch.h"

#include "tools.h"
#include "configmanager.h"

extern ConfigManager g_config;

void printXMLError(const std::string& where, const std::string& fileName, const pugi::xml_parse_result& result)
{
	std::cout << '[' << where << "] Failed to load " << fileName << ": " << result.description() << std::endl;

	FILE* file = fopen(fileName.c_str(), "rb");
	if (!file) {
		return;
	}

	char buffer[32768];
	uint32_t currentLine = 1;
	std::string line;

	size_t offset = static_cast<size_t>(result.offset);
	size_t lineOffsetPosition = 0;
	size_t index = 0;
	size_t bytes;
	do {
		bytes = fread(buffer, 1, 32768, file);
		for (size_t i = 0; i < bytes; ++i) {
			char ch = buffer[i];
			if (ch == '\n') {
				if ((index + i) >= offset) {
					lineOffsetPosition = line.length() - ((index + i) - offset);
					bytes = 0;
					break;
				}
				++currentLine;
				line.clear();
			} else {
				line.push_back(ch);
			}
		}
		index += bytes;
	} while (bytes == 32768);
	fclose(file);

	std::cout << "Line " << currentLine << ':' << std::endl;
	std::cout << line << std::endl;
	for (size_t i = 0; i < lineOffsetPosition; i++) {
		if (line[i] == '\t') {
			std::cout << '\t';
		} else {
			std::cout << ' ';
		}
	}
	std::cout << '^' << std::endl;
}

static uint32_t circularShift(int bits, uint32_t value)
{
	return (value << bits) | (value >> (32 - bits));
}

static void processSHA1MessageBlock(const uint8_t* messageBlock, uint32_t* H)
{
	uint32_t W[80];
	for (int i = 0; i < 16; ++i) {
		const size_t offset = i << 2;
		W[i] = messageBlock[offset] << 24 | messageBlock[offset + 1] << 16 | messageBlock[offset + 2] << 8 | messageBlock[offset + 3];
	}

	for (int i = 16; i < 80; ++i) {
		W[i] = circularShift(1, W[i - 3] ^ W[i - 8] ^ W[i - 14] ^ W[i - 16]);
	}

	uint32_t A = H[0], B = H[1], C = H[2], D = H[3], E = H[4];

	for (int i = 0; i < 20; ++i) {
		const uint32_t tmp = circularShift(5, A) + ((B & C) | ((~B) & D)) + E + W[i] + 0x5A827999;
		E = D; D = C; C = circularShift(30, B); B = A; A = tmp;
	}

	for (int i = 20; i < 40; ++i) {
		const uint32_t tmp = circularShift(5, A) + (B ^ C ^ D) + E + W[i] + 0x6ED9EBA1;
		E = D; D = C; C = circularShift(30, B); B = A; A = tmp;
	}

	for (int i = 40; i < 60; ++i) {
		const uint32_t tmp = circularShift(5, A) + ((B & C) | (B & D) | (C & D)) + E + W[i] + 0x8F1BBCDC;
		E = D; D = C; C = circularShift(30, B); B = A; A = tmp;
	}

	for (int i = 60; i < 80; ++i) {
		const uint32_t tmp = circularShift(5, A) + (B ^ C ^ D) + E + W[i] + 0xCA62C1D6;
		E = D; D = C; C = circularShift(30, B); B = A; A = tmp;
	}

	H[0] += A;
	H[1] += B;
	H[2] += C;
	H[3] += D;
	H[4] += E;
}

std::string transformToSHA1(const std::string& input)
{
	uint32_t H[] = {
		0x67452301,
		0xEFCDAB89,
		0x98BADCFE,
		0x10325476,
		0xC3D2E1F0
	};

	uint8_t messageBlock[64];
	size_t index = 0;

	uint32_t length_low = 0;
	uint32_t length_high = 0;
	for (char ch : input) {
		messageBlock[index++] = ch;

		length_low += 8;
		if (length_low == 0) {
			length_high++;
		}

		if (index == 64) {
			processSHA1MessageBlock(messageBlock, H);
			index = 0;
		}
	}

	messageBlock[index++] = 0x80;

	if (index > 56) {
		while (index < 64) {
			messageBlock[index++] = 0;
		}

		processSHA1MessageBlock(messageBlock, H);
		index = 0;
	}

	while (index < 56) {
		messageBlock[index++] = 0;
	}

	messageBlock[56] = length_high >> 24;
	messageBlock[57] = length_high >> 16;
	messageBlock[58] = length_high >> 8;
	messageBlock[59] = length_high;

	messageBlock[60] = length_low >> 24;
	messageBlock[61] = length_low >> 16;
	messageBlock[62] = length_low >> 8;
	messageBlock[63] = length_low;

	processSHA1MessageBlock(messageBlock, H);

	char hexstring[41];
	static const char hexDigits[] = {"0123456789abcdef"};
	for (int hashByte = 20; --hashByte >= 0;) {
		const uint8_t byte = H[hashByte >> 2] >> (((3 - hashByte) & 3) << 3);
		index = hashByte << 1;
		hexstring[index] = hexDigits[byte >> 4];
		hexstring[index + 1] = hexDigits[byte & 15];
	}
	return std::string(hexstring, 40);
}

std::string generateToken(const std::string& key, uint32_t ticks)
{
	// generate message from ticks
	std::string message(8, 0);
	for (uint8_t i = 8; --i; ticks >>= 8) {
		message[i] = static_cast<char>(ticks & 0xFF);
	}

	// hmac key pad generation
	std::string iKeyPad(64, 0x36), oKeyPad(64, 0x5C);
	for (uint8_t i = 0; i < key.length(); ++i) {
		iKeyPad[i] ^= key[i];
		oKeyPad[i] ^= key[i];
	}

	oKeyPad.reserve(84);

	// hmac concat inner pad with message
	iKeyPad.append(message);

	// hmac first pass
	message.assign(transformToSHA1(iKeyPad));

	// hmac concat outer pad with message, conversion from hex to int needed
	for (uint8_t i = 0; i < message.length(); i += 2) {
		oKeyPad.push_back(static_cast<char>(std::strtoul(message.substr(i, 2).c_str(), nullptr, 16)));
	}

	// hmac second pass
	message.assign(transformToSHA1(oKeyPad));

	// calculate hmac offset
	uint32_t offset = static_cast<uint32_t>(std::strtoul(message.substr(39, 1).c_str(), nullptr, 16) & 0xF);

	// get truncated hash
	uint32_t truncHash = static_cast<uint32_t>(std::strtoul(message.substr(2 * offset, 8).c_str(), nullptr, 16)) & 0x7FFFFFFF;
	message.assign(std::to_string(truncHash));

	// return only last AUTHENTICATOR_DIGITS (default 6) digits, also asserts exactly 6 digits
	uint32_t hashLen = message.length();
	message.assign(message.substr(hashLen - std::min(hashLen, AUTHENTICATOR_DIGITS)));
	message.insert(0, AUTHENTICATOR_DIGITS - std::min(hashLen, AUTHENTICATOR_DIGITS), '0');
	return message;
}

void replaceString(std::string& str, const std::string& sought, const std::string& replacement)
{
	size_t pos = 0;
	size_t start = 0;
	size_t soughtLen = sought.length();
	size_t replaceLen = replacement.length();

	while ((pos = str.find(sought, start)) != std::string::npos) {
		str = str.substr(0, pos) + replacement + str.substr(pos + soughtLen);
		start = pos + replaceLen;
	}
}

void trim_right(std::string& source, char t)
{
	source.erase(source.find_last_not_of(t) + 1);
}

void trim_left(std::string& source, char t)
{
	source.erase(0, source.find_first_not_of(t));
}

void toLowerCaseString(std::string& source)
{
	std::transform(source.begin(), source.end(), source.begin(), tolower);
}

std::string asLowerCaseString(std::string source)
{
	toLowerCaseString(source);
	return source;
}

std::string asUpperCaseString(std::string source)
{
	std::transform(source.begin(), source.end(), source.begin(), toupper);
	return source;
}

StringVector explodeString(const std::string& inString, const std::string& separator, int32_t limit/* = -1*/)
{
	StringVector returnVector;
	std::string::size_type start = 0, end = 0;

	while (--limit != -1 && (end = inString.find(separator, start)) != std::string::npos) {
		returnVector.emplace_back(std::move(inString.substr(start, end - start)));
		start = end + separator.size();
	}

	returnVector.emplace_back(std::move(inString.substr(start)));
	return returnVector;
}

IntegerVector vectorAtoi(const StringVector& stringVector)
{
	IntegerVector returnVector;
	for (const auto& string : stringVector) {
		returnVector.push_back(std::stoi(string));
	}
	return returnVector;
}

std::mt19937& getRandomGenerator()
{
	static std::random_device rd;
	static std::mt19937 generator(rd());
	return generator;
}

int32_t uniform_random(int32_t minNumber, int32_t maxNumber)
{
	static std::uniform_int_distribution<int32_t> uniformRand;
	if (minNumber == maxNumber) {
		return minNumber;
	} else if (minNumber > maxNumber) {
		std::swap(minNumber, maxNumber);
	}
	return uniformRand(getRandomGenerator(), std::uniform_int_distribution<int32_t>::param_type(minNumber, maxNumber));
}

int32_t normal_random(int32_t minNumber, int32_t maxNumber)
{
	static std::normal_distribution<float> normalRand(0.5f, 0.25f);
	if (minNumber == maxNumber) {
		return minNumber;
	} else if (minNumber > maxNumber) {
		std::swap(minNumber, maxNumber);
	}

	int32_t increment;
	const int32_t diff = maxNumber - minNumber;
	const float v = normalRand(getRandomGenerator());
	if (v < 0.0) {
		increment = diff / 2;
	} else if (v > 1.0) {
		increment = (diff + 1) / 2;
	} else {
		increment = round(v * diff);
	}
	return minNumber + increment;
}

bool boolean_random(double probability/* = 0.5*/)
{
	static std::bernoulli_distribution booleanRand;
	return booleanRand(getRandomGenerator(), std::bernoulli_distribution::param_type(probability));
}

void trimString(std::string& str)
{
	str.erase(str.find_last_not_of(' ') + 1);
	str.erase(0, str.find_first_not_of(' '));
}

std::string convertIPToString(uint32_t ip)
{
	char buffer[17];

	int res = sprintf(buffer, "%u.%u.%u.%u", ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, (ip >> 24));
	if (res < 0) {
		return {};
	}

	return buffer;
}

std::string formatDate(time_t time)
{
	const tm* tms = localtime(&time);
	if (!tms) {
		return {};
	}

	char buffer[20];
	int res = sprintf(buffer, "%02d/%02d/%04d %02d:%02d:%02d", tms->tm_mday, tms->tm_mon + 1, tms->tm_year + 1900, tms->tm_hour, tms->tm_min, tms->tm_sec);
	if (res < 0) {
		return {};
	}
	return {buffer, 19};
}

std::string formatDateShort(time_t time)
{
	const tm* tms = localtime(&time);
	if (!tms) {
		return {};
	}

	char buffer[12];
	size_t res = strftime(buffer, 12, "%d %b %Y", tms);
	if (res == 0) {
		return {};
	}
	return {buffer, 11};
}

Direction getDirection(const std::string& string)
{
	Direction direction = DIRECTION_NORTH;

	if (string == "norte" || string == "n" || string == "0") {
		direction = DIRECTION_NORTH;
	} else if (string == "leste" || string == "l" || string == "1") {
		direction = DIRECTION_EAST;
	} else if (string == "sul" || string == "s" || string == "2") {
		direction = DIRECTION_SOUTH;
	} else if (string == "oeste" || string == "o" || string == "3") {
		direction = DIRECTION_WEST;
	} else if (string == "sudoeste" || string == "south west" || string == "south-west" || string == "so" || string == "4") {
		direction = DIRECTION_SOUTHWEST;
	} else if (string == "sudeste" || string == "south east" || string == "south-east" || string == "se" || string == "5") {
		direction = DIRECTION_SOUTHEAST;
	} else if (string == "noroeste" || string == "north west" || string == "north-west" || string == "no" || string == "6") {
		direction = DIRECTION_NORTHWEST;
	} else if (string == "nordeste" || string == "north east" || string == "north-east" || string == "ne" || string == "7") {
		direction = DIRECTION_NORTHEAST;
	}

	return direction;
}

Position getNextPosition(Direction direction, Position pos)
{
	switch (direction) {
		case DIRECTION_NORTH:
			pos.y--;
			break;

		case DIRECTION_SOUTH:
			pos.y++;
			break;

		case DIRECTION_WEST:
			pos.x--;
			break;

		case DIRECTION_EAST:
			pos.x++;
			break;

		case DIRECTION_SOUTHWEST:
			pos.x--;
			pos.y++;
			break;

		case DIRECTION_NORTHWEST:
			pos.x--;
			pos.y--;
			break;

		case DIRECTION_NORTHEAST:
			pos.x++;
			pos.y--;
			break;

		case DIRECTION_SOUTHEAST:
			pos.x++;
			pos.y++;
			break;

		default:
			break;
	}

	return pos;
}

Direction getDirectionTo(const Position& from, const Position& to)
{
	Direction dir;

	int32_t x_offset = Position::getOffsetX(from, to);
	if (x_offset < 0) {
		dir = DIRECTION_EAST;
		x_offset = std::abs(x_offset);
	} else {
		dir = DIRECTION_WEST;
	}

	int32_t y_offset = Position::getOffsetY(from, to);
	if (y_offset >= 0) {
		if (y_offset > x_offset) {
			dir = DIRECTION_NORTH;
		} else if (y_offset == x_offset) {
			if (dir == DIRECTION_EAST) {
				dir = DIRECTION_NORTHEAST;
			} else {
				dir = DIRECTION_NORTHWEST;
			}
		}
	} else {
		y_offset = std::abs(y_offset);
		if (y_offset > x_offset) {
			dir = DIRECTION_SOUTH;
		} else if (y_offset == x_offset) {
			if (dir == DIRECTION_EAST) {
				dir = DIRECTION_SOUTHEAST;
			} else {
				dir = DIRECTION_SOUTHWEST;
			}
		}
	}
	return dir;
}

using MagicEffectNames = std::unordered_map<std::string, MagicEffectClasses>;
using ShootTypeNames = std::unordered_map<std::string, ShootType_t>;
using CombatTypeNames = std::unordered_map<CombatType_t, std::string, std::hash<int32_t>>;
using AmmoTypeNames = std::unordered_map<std::string, Ammo_t>;
using WeaponActionNames = std::unordered_map<std::string, WeaponAction_t>;
using SkullNames = std::unordered_map<std::string, Skulls_t>;

MagicEffectNames magicEffectNames = {
	{"redspark",		CONST_ME_DRAWBLOOD},
	{"bluebubble",		CONST_ME_LOSEENERGY},
	{"poff",		CONST_ME_POFF},
	{"yellowspark",		CONST_ME_BLOCKHIT},
	{"explosionarea",	CONST_ME_EXPLOSIONAREA},
	{"explosion",		CONST_ME_EXPLOSIONHIT},
	{"firearea",		CONST_ME_FIREAREA},
	{"yellowbubble",	CONST_ME_YELLOW_RINGS},
	{"greenbubble",		CONST_ME_GREEN_RINGS},
	{"blackspark",		CONST_ME_HITAREA},
	{"teleport",		CONST_ME_TELEPORT},
	{"energy",		CONST_ME_ENERGYHIT},
	{"blueshimmer",		CONST_ME_MAGIC_BLUE},
	{"redshimmer",		CONST_ME_MAGIC_RED},
	{"greenshimmer",	CONST_ME_MAGIC_GREEN},
	{"fire",		CONST_ME_HITBYFIRE},
	{"greenspark",		CONST_ME_HITBYPOISON},
	{"mortarea",		CONST_ME_MORTAREA},
	{"greennote",		CONST_ME_SOUND_GREEN},
	{"rednote",		CONST_ME_SOUND_RED},
	{"poison",		CONST_ME_POISONAREA},
	{"yellownote",		CONST_ME_SOUND_YELLOW},
	{"purplenote",		CONST_ME_SOUND_PURPLE},
	{"bluenote",		CONST_ME_SOUND_BLUE},
	{"whitenote",		CONST_ME_SOUND_WHITE},
	{"bubbles",		CONST_ME_BUBBLES},
	{"dice",		CONST_ME_CRAPS},
	{"giftwraps",		CONST_ME_GIFT_WRAPS},
	{"yellowfirework",	CONST_ME_FIREWORK_YELLOW},
	{"redfirework",		CONST_ME_FIREWORK_RED},
	{"bluefirework",	CONST_ME_FIREWORK_BLUE},
	{"stun",		CONST_ME_STUN},
	{"sleep",		CONST_ME_SLEEP},
	{"watercreature",	CONST_ME_WATERCREATURE},
	{"groundshaker",	CONST_ME_GROUNDSHAKER},
	{"hearts",		CONST_ME_HEARTS},
	{"fireattack",		CONST_ME_FIREATTACK},
	{"energyarea",		CONST_ME_ENERGYAREA},
	{"smallclouds",		CONST_ME_SMALLCLOUDS},
	{"holydamage",		CONST_ME_HOLYDAMAGE},
	{"bigclouds",		CONST_ME_BIGCLOUDS},
	{"icearea",		CONST_ME_ICEAREA},
	{"icetornado",		CONST_ME_ICETORNADO},
	{"iceattack",		CONST_ME_ICEATTACK},
	{"stones",		CONST_ME_STONES},
	{"smallplants",		CONST_ME_SMALLPLANTS},
	{"carniphila",		CONST_ME_CARNIPHILA},
	{"purpleenergy",	CONST_ME_PURPLEENERGY},
	{"yellowenergy",	CONST_ME_YELLOWENERGY},
	{"holyarea",		CONST_ME_HOLYAREA},
	{"bigplants",		CONST_ME_BIGPLANTS},
	{"cake",		CONST_ME_CAKE},
	{"giantice",		CONST_ME_GIANTICE},
	{"watersplash",		CONST_ME_WATERSPLASH},
	{"plantattack",		CONST_ME_PLANTATTACK},
	{"tutorialarrow",	CONST_ME_TUTORIALARROW},
	{"tutorialsquare",	CONST_ME_TUTORIALSQUARE},
	{"mirrorhorizontal",	CONST_ME_MIRRORHORIZONTAL},
	{"mirrorvertical",	CONST_ME_MIRRORVERTICAL},
	{"skullhorizontal",	CONST_ME_SKULLHORIZONTAL},
	{"skullvertical",	CONST_ME_SKULLVERTICAL},
	{"assassin",		CONST_ME_ASSASSIN},
	{"stepshorizontal",	CONST_ME_STEPSHORIZONTAL},
	{"bloodysteps",		CONST_ME_BLOODYSTEPS},
	{"stepsvertical",	CONST_ME_STEPSVERTICAL},
	{"yalaharighost",	CONST_ME_YALAHARIGHOST},
	{"bats",		CONST_ME_BATS},
	{"smoke",		CONST_ME_SMOKE},
	{"insects",		CONST_ME_INSECTS},
	{"dragonhead",		CONST_ME_DRAGONHEAD},
};

ShootTypeNames shootTypeNames = {
	{"spear",		CONST_ANI_SPEAR},
	{"bolt",		CONST_ANI_BOLT},
	{"arrow",		CONST_ANI_ARROW},
	{"fire",		CONST_ANI_FIRE},
	{"energy",		CONST_ANI_ENERGY},
	{"poisonarrow",		CONST_ANI_POISONARROW},
	{"burstarrow",		CONST_ANI_BURSTARROW},
	{"throwingstar",	CONST_ANI_THROWINGSTAR},
	{"throwingknife",	CONST_ANI_THROWINGKNIFE},
	{"smallstone",		CONST_ANI_SMALLSTONE},
	{"death",		CONST_ANI_DEATH},
	{"largerock",		CONST_ANI_LARGEROCK},
	{"snowball",		CONST_ANI_SNOWBALL},
	{"powerbolt",		CONST_ANI_POWERBOLT},
	{"poison",		CONST_ANI_POISON},
	{"infernalbolt",	CONST_ANI_INFERNALBOLT},
	{"huntingspear",	CONST_ANI_HUNTINGSPEAR},
	{"enchantedspear",	CONST_ANI_ENCHANTEDSPEAR},
	{"redstar",		CONST_ANI_REDSTAR},
	{"greenstar",		CONST_ANI_GREENSTAR},
	{"royalspear",		CONST_ANI_ROYALSPEAR},
	{"sniperarrow",		CONST_ANI_SNIPERARROW},
	{"onyxarrow",		CONST_ANI_ONYXARROW},
	{"piercingbolt",	CONST_ANI_PIERCINGBOLT},
	{"whirlwindsword",	CONST_ANI_WHIRLWINDSWORD},
	{"whirlwindaxe",	CONST_ANI_WHIRLWINDAXE},
	{"whirlwindclub",	CONST_ANI_WHIRLWINDCLUB},
	{"etherealspear",	CONST_ANI_ETHEREALSPEAR},
	{"ice",			CONST_ANI_ICE},
	{"earth",		CONST_ANI_EARTH},
	{"holy",		CONST_ANI_HOLY},
	{"suddendeath",		CONST_ANI_SUDDENDEATH},
	{"flasharrow",		CONST_ANI_FLASHARROW},
	{"flammingarrow",	CONST_ANI_FLAMMINGARROW},
	{"shiverarrow",		CONST_ANI_SHIVERARROW},
	{"energyball",		CONST_ANI_ENERGYBALL},
	{"smallice",		CONST_ANI_SMALLICE},
	{"smallholy",		CONST_ANI_SMALLHOLY},
	{"smallearth",		CONST_ANI_SMALLEARTH},
	{"eartharrow",		CONST_ANI_EARTHARROW},
	{"explosion",		CONST_ANI_EXPLOSION},
};

CombatTypeNames combatTypeNames = {
	{COMBAT_PHYSICALDAMAGE, 	"physical"},
	{COMBAT_ENERGYDAMAGE, 		"energy"},
	{COMBAT_EARTHDAMAGE, 		"earth"},
	{COMBAT_FIREDAMAGE, 		"fire"},
	{COMBAT_UNDEFINEDDAMAGE, 	"undefined"},
	{COMBAT_LIFEDRAIN, 		"lifedrain"},
	{COMBAT_MANADRAIN, 		"manadrain"},
	{COMBAT_HEALING, 		"healing"},
	{COMBAT_DROWNDAMAGE, 		"drown"},
	{COMBAT_ICEDAMAGE, 		"ice"},
	{COMBAT_HOLYDAMAGE, 		"holy"},
	{COMBAT_DEATHDAMAGE, 		"death"},
};

AmmoTypeNames ammoTypeNames = {
	{"spear",		AMMO_SPEAR},
	{"bolt",		AMMO_BOLT},
	{"arrow",		AMMO_ARROW},
	{"poisonarrow",		AMMO_ARROW},
	{"burstarrow",		AMMO_ARROW},
	{"throwingstar",	AMMO_THROWINGSTAR},
	{"throwingknife",	AMMO_THROWINGKNIFE},
	{"smallstone",		AMMO_STONE},
	{"largerock",		AMMO_STONE},
	{"snowball",		AMMO_SNOWBALL},
	{"powerbolt",		AMMO_BOLT},
	{"infernalbolt",	AMMO_BOLT},
	{"huntingspear",	AMMO_SPEAR},
	{"enchantedspear",	AMMO_SPEAR},
	{"royalspear",		AMMO_SPEAR},
	{"sniperarrow",		AMMO_ARROW},
	{"onyxarrow",		AMMO_ARROW},
	{"piercingbolt",	AMMO_BOLT},
	{"etherealspear",	AMMO_SPEAR},
	{"flasharrow",		AMMO_ARROW},
	{"flammingarrow",	AMMO_ARROW},
	{"shiverarrow",		AMMO_ARROW},
	{"eartharrow",		AMMO_ARROW},
};

WeaponActionNames weaponActionNames = {
	{"move",		WEAPONACTION_MOVE},
	{"removecharge",	WEAPONACTION_REMOVECHARGE},
	{"removecount",		WEAPONACTION_REMOVECOUNT},
};

SkullNames skullNames = {
	{"none",	SKULL_NONE},
	{"yellow",	SKULL_YELLOW},
	{"green",	SKULL_GREEN},
	{"white",	SKULL_WHITE},
	{"red",		SKULL_RED},
	{"black",	SKULL_BLACK},
};

MagicEffectClasses getMagicEffect(const std::string& strValue)
{
	auto magicEffect = magicEffectNames.find(strValue);
	if (magicEffect != magicEffectNames.end()) {
		return magicEffect->second;
	}
	return CONST_ME_NONE;
}

ShootType_t getShootType(const std::string& strValue)
{
	auto shootType = shootTypeNames.find(strValue);
	if (shootType != shootTypeNames.end()) {
		return shootType->second;
	}
	return CONST_ANI_NONE;
}

std::string getCombatName(CombatType_t combatType)
{
	auto combatName = combatTypeNames.find(combatType);
	if (combatName != combatTypeNames.end()) {
		return combatName->second;
	}
	return "unknown";
}

Ammo_t getAmmoType(const std::string& strValue)
{
	auto ammoType = ammoTypeNames.find(strValue);
	if (ammoType != ammoTypeNames.end()) {
		return ammoType->second;
	}
	return AMMO_NONE;
}

WeaponAction_t getWeaponAction(const std::string& strValue)
{
	auto weaponAction = weaponActionNames.find(strValue);
	if (weaponAction != weaponActionNames.end()) {
		return weaponAction->second;
	}
	return WEAPONACTION_NONE;
}

Skulls_t getSkullType(const std::string& strValue)
{
	auto skullType = skullNames.find(strValue);
	if (skullType != skullNames.end()) {
		return skullType->second;
	}
	return SKULL_NONE;
}

std::string getSkillName(uint8_t skillid)
{
	switch (skillid) {
		case SKILL_FIST:
			return "fist fighting";

		case SKILL_CLUB:
			return "club fighting";

		case SKILL_SWORD:
			return "sword fighting";

		case SKILL_AXE:
			return "axe fighting";

		case SKILL_DISTANCE:
			return "distance fighting";

		case SKILL_SHIELD:
			return "shielding";

		case SKILL_FISHING:
			return "fishing";

		case SKILL_MAGLEVEL:
			return "magic level";

		case SKILL_LEVEL:
			return "level";

		default:
			return "unknown";
	}
}

uint32_t adlerChecksum(const uint8_t* data, size_t length)
{
	if (length > NETWORKMESSAGE_MAXSIZE) {
		return 0;
	}

	const uint16_t adler = 65521;

	uint32_t a = 1, b = 0;

	while (length > 0) {
		size_t tmp = length > 5552 ? 5552 : length;
		length -= tmp;

		do {
			a += *data++;
			b += a;
		} while (--tmp);

		a %= adler;
		b %= adler;
	}

	return (b << 16) | a;
}

std::string ucfirst(std::string str)
{
	for (char& i : str) {
		if (i != ' ') {
			i = toupper(i);
			break;
		}
	}
	return str;
}

std::string ucwords(std::string str)
{
	size_t strLength = str.length();
	if (strLength == 0) {
		return str;
	}

	str[0] = toupper(str.front());
	for (size_t i = 1; i < strLength; ++i) {
		if (str[i - 1] == ' ') {
			str[i] = toupper(str[i]);
		}
	}

	return str;
}

bool booleanString(const std::string& str)
{
	if (str.empty()) {
		return false;
	}

	char ch = tolower(str.front());
	return ch != 'f' && ch != 'n' && ch != '0';
}

std::string getWeaponName(WeaponType_t weaponType)
{
	switch (weaponType) {
		case WEAPON_SWORD: return "sword";
		case WEAPON_CLUB: return "club";
		case WEAPON_AXE: return "axe";
		case WEAPON_DISTANCE: return "distance";
		case WEAPON_WAND: return "wand";
		case WEAPON_AMMO: return "ammunition";
		default: return std::string();
	}
}

size_t combatTypeToIndex(CombatType_t combatType)
{
	switch (combatType) {
		case COMBAT_PHYSICALDAMAGE:
			return 0;
		case COMBAT_ENERGYDAMAGE:
			return 1;
		case COMBAT_EARTHDAMAGE:
			return 2;
		case COMBAT_FIREDAMAGE:
			return 3;
		case COMBAT_UNDEFINEDDAMAGE:
			return 4;
		case COMBAT_LIFEDRAIN:
			return 5;
		case COMBAT_MANADRAIN:
			return 6;
		case COMBAT_HEALING:
			return 7;
		case COMBAT_DROWNDAMAGE:
			return 8;
		case COMBAT_ICEDAMAGE:
			return 9;
		case COMBAT_HOLYDAMAGE:
			return 10;
		case COMBAT_DEATHDAMAGE:
			return 11;
		default:
			return 0;
	}
}

CombatType_t indexToCombatType(size_t v)
{
	return static_cast<CombatType_t>(1 << v);
}

uint8_t serverFluidToClient(uint8_t serverFluid)
{
	uint8_t size = sizeof(clientToServerFluidMap) / sizeof(uint8_t);
	for (uint8_t i = 0; i < size; ++i) {
		if (clientToServerFluidMap[i] == serverFluid) {
			return i;
		}
	}
	return 0;
}

uint8_t clientFluidToServer(uint8_t clientFluid)
{
	uint8_t size = sizeof(clientToServerFluidMap) / sizeof(uint8_t);
	if (clientFluid >= size) {
		return 0;
	}
	return clientToServerFluidMap[clientFluid];
}

itemAttrTypes stringToItemAttribute(const std::string& str)
{
	if (str == "aid") {
		return ITEM_ATTRIBUTE_ACTIONID;
	} else if (str == "uid") {
		return ITEM_ATTRIBUTE_UNIQUEID;
	} else if (str == "description") {
		return ITEM_ATTRIBUTE_DESCRIPTION;
	} else if (str == "text") {
		return ITEM_ATTRIBUTE_TEXT;
	} else if (str == "date") {
		return ITEM_ATTRIBUTE_DATE;
	} else if (str == "writer") {
		return ITEM_ATTRIBUTE_WRITER;
	} else if (str == "name") {
		return ITEM_ATTRIBUTE_NAME;
	} else if (str == "article") {
		return ITEM_ATTRIBUTE_ARTICLE;
	} else if (str == "pluralname") {
		return ITEM_ATTRIBUTE_PLURALNAME;
	} else if (str == "weight") {
		return ITEM_ATTRIBUTE_WEIGHT;
	} else if (str == "attack") {
		return ITEM_ATTRIBUTE_ATTACK;
	} else if (str == "defense") {
		return ITEM_ATTRIBUTE_DEFENSE;
	} else if (str == "extradefense") {
		return ITEM_ATTRIBUTE_EXTRADEFENSE;
	} else if (str == "armor") {
		return ITEM_ATTRIBUTE_ARMOR;
	} else if (str == "hitchance") {
		return ITEM_ATTRIBUTE_HITCHANCE;
	} else if (str == "shootrange") {
		return ITEM_ATTRIBUTE_SHOOTRANGE;
	} else if (str == "owner") {
		return ITEM_ATTRIBUTE_OWNER;
	} else if (str == "duration") {
		return ITEM_ATTRIBUTE_DURATION;
	} else if (str == "decaystate") {
		return ITEM_ATTRIBUTE_DECAYSTATE;
	} else if (str == "corpseowner") {
		return ITEM_ATTRIBUTE_CORPSEOWNER;
	} else if (str == "charges") {
		return ITEM_ATTRIBUTE_CHARGES;
	} else if (str == "fluidtype") {
		return ITEM_ATTRIBUTE_FLUIDTYPE;
	} else if (str == "doorid") {
		return ITEM_ATTRIBUTE_DOORID;
	}
	return ITEM_ATTRIBUTE_NONE;
}

std::string getFirstLine(const std::string& str)
{
	std::string firstLine;
	firstLine.reserve(str.length());
	for (const char c : str) {
		if (c == '\n') {
			break;
		}
		firstLine.push_back(c);
	}
	return firstLine;
}

const char* getReturnMessage(ReturnValue value)
{
	switch (value) {
		case RETURNVALUE_REWARDCHESTISEMPTY:
			return "O ba� est� vazio no momento. Voc� n�o participou de nenhuma batalha nos �ltimos sete dias ou j� reivindicou sua recompensa.";
			
		case RETURNVALUE_DESTINATIONOUTOFREACH:
			return "O destino est� fora do alcance.";

		case RETURNVALUE_NOTMOVEABLE:
			return "Voc� n�o pode mover este objeto.";

		case RETURNVALUE_DROPTWOHANDEDITEM:
			return "Solte o objeto de duas m�os primeiro.";

		case RETURNVALUE_BOTHHANDSNEEDTOBEFREE:
			return "Ambas as m�os precisam estar livres.";

		case RETURNVALUE_CANNOTBEDRESSED:
			return "Voc� n�o pode vestir este objeto.";

		case RETURNVALUE_PUTTHISOBJECTINYOURHAND:
			return "Coloque esse objeto na sua m�o.";

		case RETURNVALUE_PUTTHISOBJECTINBOTHHANDS:
			return "Coloque este objeto nas duas m�os.";

		case RETURNVALUE_CANONLYUSEONEWEAPON:
			return "Voc� pode usar apenas uma arma.";

		case RETURNVALUE_TOOFARAWAY:
			return "Voc� est� muito longe.";

		case RETURNVALUE_FIRSTGODOWNSTAIRS:
			return "Primeiro des�a as escadas.";

		case RETURNVALUE_FIRSTGOUPSTAIRS:
			return "Primeiro suba as escadas.";

		case RETURNVALUE_NOTENOUGHCAPACITY:
			return "Este objeto � muito pesado para voc� carregar.";

		case RETURNVALUE_CONTAINERNOTENOUGHROOM:
			return "Voc� n�o pode colocar mais objetos neste cont�iner.";

		case RETURNVALUE_NEEDEXCHANGE:
		case RETURNVALUE_NOTENOUGHROOM:
			return "N�o h� espa�o suficiente.";

		case RETURNVALUE_CANNOTPICKUP:
			return "Voc� n�o pode pegar este objeto.";

		case RETURNVALUE_CANNOTTHROW:
			return "Voc� n�o pode jogar l�.";

		case RETURNVALUE_THEREISNOWAY:
			return "N�o tem jeito.";

		case RETURNVALUE_THISISIMPOSSIBLE:
			return "Isto � imposs�vel.";

		case RETURNVALUE_PLAYERISPZLOCKED:
			return "Voc� n�o pode entrar em uma zona de prote��o depois de atacar outro jogador.";

		case RETURNVALUE_PLAYERISNOTINVITED:
			return "Voc� n�o est� convidado.";

		case RETURNVALUE_CREATUREDOESNOTEXIST:
			return "Criatura n�o existe.";

		case RETURNVALUE_DEPOTISFULL:
			return "Voc� n�o pode colocar mais itens neste depot.";

		case RETURNVALUE_CANNOTUSETHISOBJECT:
			return "Voc� n�o pode usar este objeto.";

		case RETURNVALUE_PLAYERWITHTHISNAMEISNOTONLINE:
			return "Um jogador com este nome n�o est� online.";

		case RETURNVALUE_NOTREQUIREDLEVELTOUSERUNE:
			return "Voc� n�o tem magic level necess�rio para usar esta runa.";

		case RETURNVALUE_YOUAREALREADYTRADING:
			return "Voc� j� est� negociando.";

		case RETURNVALUE_THISPLAYERISALREADYTRADING:
			return "Este jogador j� est� negociando.";

		case RETURNVALUE_YOUMAYNOTLOGOUTDURINGAFIGHT:
			return "Voc� n�o pode sair durante ou imediatamente ap�s uma luta!";

		case RETURNVALUE_DIRECTPLAYERSHOOT:
			return "Voc� n�o tem permiss�o para atirar diretamente nos jogadores.";

		case RETURNVALUE_NOTENOUGHLEVEL:
			return "Seu n�vel est� muito baixo.";

		case RETURNVALUE_NOTENOUGHMAGICLEVEL:
			return "Voc� n�o tem magic level suficiente.";

		case RETURNVALUE_NOTENOUGHMANA:
			return "Voc� n�o tem mana suficiente.";

		case RETURNVALUE_NOTENOUGHSOUL:
			return "Voc� n�o tem soul suficiente.";

		case RETURNVALUE_YOUAREEXHAUSTED:
			return "Voc� est� exausto.";

		case RETURNVALUE_YOUCANNOTUSEOBJECTSTHATFAST:
			return "Voc� n�o pode usar objetos t�o r�pido.";

		case RETURNVALUE_CANONLYUSETHISRUNEONCREATURES:
			return "Voc� s� pode us�-lo em criaturas.";

		case RETURNVALUE_PLAYERISNOTREACHABLE:
			return "O jogador n�o est� acess�vel.";

		case RETURNVALUE_CREATUREISNOTREACHABLE:
			return "Criatura n�o � alcan��vel.";

		case RETURNVALUE_ACTIONNOTPERMITTEDINPROTECTIONZONE:
			return "Esta a��o n�o � permitida em uma zona de prote��o.";

		case RETURNVALUE_YOUMAYNOTATTACKTHISPLAYER:
			return "Voc� n�o pode atacar essa pessoa.";

		case RETURNVALUE_YOUMAYNOTATTACKTHISCREATURE:
			return "Voc� n�o pode atacar esta criatura.";

		case RETURNVALUE_YOUMAYNOTATTACKAPERSONINPROTECTIONZONE:
			return "Voc� n�o pode atacar uma pessoa em uma zona de prote��o.";

		case RETURNVALUE_YOUMAYNOTATTACKAPERSONWHILEINPROTECTIONZONE:
			return "Voc� n�o pode atacar uma pessoa enquanto estiver em uma zona de prote��o.";

		case RETURNVALUE_YOUCANONLYUSEITONCREATURES:
			return "Voc� s� pode us�-lo em criaturas.";

		case RETURNVALUE_TURNSECUREMODETOATTACKUNMARKEDPLAYERS:
			return "Desative o modo seguro se voc� realmente quiser atacar jogadores n�o marcados.";

		case RETURNVALUE_YOUNEEDPREMIUMACCOUNT:
			return "Voc� precisa de uma conta premium.";

		case RETURNVALUE_YOUNEEDTOLEARNTHISSPELL:
			return "Voc� deve aprender esse feiti�o primeiro.";

		case RETURNVALUE_YOURVOCATIONCANNOTUSETHISSPELL:
			return "Voc� tem a voca��o errada para lan�ar este feiti�o.";

		case RETURNVALUE_YOUNEEDAWEAPONTOUSETHISSPELL:
			return "Voc� precisa equipar uma arma para usar esse feiti�o.";

		case RETURNVALUE_PLAYERISPZLOCKEDLEAVEPVPZONE:
			return "Voc� n�o pode sair de uma zona pvp depois de atacar outro jogador.";

		case RETURNVALUE_PLAYERISPZLOCKEDENTERPVPZONE:
			return "Voc� n�o pode entrar em uma zona pvp depois de atacar outro jogador.";

		case RETURNVALUE_ACTIONNOTPERMITTEDINANOPVPZONE:
			return "Esta a��o n�o � permitida em uma zona noo pvp.";

		case RETURNVALUE_YOUCANNOTLOGOUTHERE:
			return "Voc� n�o pode deslogar aqui.";

		case RETURNVALUE_YOUNEEDAMAGICITEMTOCASTSPELL:
			return "Voc� precisa de um item m�gico para lan�ar este feiti�o.";

		case RETURNVALUE_CANNOTCONJUREITEMHERE:
			return "Voc� n�o pode conjurar itens aqui.";

		case RETURNVALUE_YOUNEEDTOSPLITYOURSPEARS:
			return "Voc� precisa dividir suas lan�as primeiro.";

		case RETURNVALUE_NAMEISTOOAMBIGUOUS:
			return "O nome do jogador � amb�guo.";

		case RETURNVALUE_CANONLYUSEONESHIELD:
			return "Voc� pode usar apenas um escudo.";

		case RETURNVALUE_NOPARTYMEMBERSINRANGE:
			return "Nenhum membro do grupo dentro do alcance.";

		case RETURNVALUE_YOUARENOTTHEOWNER:
			return "Voc� n�o � o dono.";

		case RETURNVALUE_NOSUCHRAIDEXISTS:
			return "N�o existe essa raid.";

		case RETURNVALUE_ANOTHERRAIDISALREADYEXECUTING:
			return "Outra raid j� est� em execu��o.";

		case RETURNVALUE_TRADEPLAYERFARAWAY:
			return "Trade player is too far away.";

		case RETURNVALUE_YOUDONTOWNTHISHOUSE:
			return "Voc� n�o � dono desta casa.";

		case RETURNVALUE_TRADEPLAYERALREADYOWNSAHOUSE:
			return "Trade player already owns a house.";

		case RETURNVALUE_TRADEPLAYERHIGHESTBIDDER:
			return "Trade player is currently the highest bidder of an auctioned house.";

		case RETURNVALUE_YOUCANNOTTRADETHISHOUSE:
			return "Voc� n�o pode trocar esta casa.";

		case RETURNVALUE_YOUDONTHAVEREQUIREDPROFESSION:
			return "Voc� n�o tem a profiss�o necess�ria.";

		default: // RETURNVALUE_NOTPOSSIBLE, etc
			return "Desculpe, n�o � poss�vel.";
	}
}

int64_t OTSYS_TIME()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

SpellGroup_t stringToSpellGroup(std::string value)
{
	std::string tmpStr = asLowerCaseString(value);
	if (tmpStr == "attack" || tmpStr == "1") {
		return SPELLGROUP_ATTACK;
	} else if (tmpStr == "healing" || tmpStr == "2") {
		return SPELLGROUP_HEALING;
	} else if (tmpStr == "support" || tmpStr == "3") {
		return SPELLGROUP_SUPPORT;
	} else if (tmpStr == "special" || tmpStr == "4") {
		return SPELLGROUP_SPECIAL;
	}
	return SPELLGROUP_NONE;
}

#if defined(__SSE4_2__)
int tfs_strncmp(const char* s1, const char* s2, size_t n)
{
	__m128i* ptr1 = reinterpret_cast<__m128i*>(const_cast<char*>(s1));
	__m128i* ptr2 = reinterpret_cast<__m128i*>(const_cast<char*>(s2));

	const uint8_t mode = (_SIDD_UBYTE_OPS | _SIDD_CMP_EQUAL_EACH | _SIDD_NEGATIVE_POLARITY | _SIDD_LEAST_SIGNIFICANT);
	for (; n != 0; ++ptr1, ++ptr2) {
		const __m128i a = _mm_loadu_si128(ptr1);
		const __m128i b = _mm_loadu_si128(ptr2);
		if (_mm_cmpestrc(a, n, b, n, mode)) {
			const auto idx = _mm_cmpestri(a, n, b, n, mode);

			const uint8_t b1 = (reinterpret_cast<char*>(ptr1))[idx];
			const uint8_t b2 = (reinterpret_cast<char*>(ptr2))[idx];
			if (b1 < b2) {
				return -1;
			} else if (b1 > b2) {
				return 1;
			} else {
				return 0;
			}
		}
		n = (n > 16 ? n - 16 : 0);
	}
	return 0;
}

int tfs_strcmp(const char* s1, const char* s2)
{
	__m128i* ptr1 = reinterpret_cast<__m128i*>(const_cast<char*>(s1));
	__m128i* ptr2 = reinterpret_cast<__m128i*>(const_cast<char*>(s2));

	const uint8_t mode = (_SIDD_UBYTE_OPS | _SIDD_CMP_EQUAL_EACH | _SIDD_NEGATIVE_POLARITY | _SIDD_LEAST_SIGNIFICANT);
	for (; ; ++ptr1, ++ptr2) {
		const __m128i a = _mm_loadu_si128(ptr1);
		const __m128i b = _mm_loadu_si128(ptr2);
		if (_mm_cmpistrc(a, b, mode)) {
			const auto idx = _mm_cmpistri(a, b, mode);

			const uint8_t b1 = (reinterpret_cast<char*>(ptr1))[idx];
			const uint8_t b2 = (reinterpret_cast<char*>(ptr2))[idx];
			if (b1 < b2) {
				return -1;
			} else if (b1 > b2) {
				return 1;
			} else {
				return 0;
			}
		} else if (_mm_cmpistrz(a, b, mode)) {
			break;
		}
	}
	return 0;
}
#endif