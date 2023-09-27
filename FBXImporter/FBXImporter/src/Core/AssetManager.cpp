#include "AssetManager.h"
#include <vector>
#include <iostream>
#include "../Common.h"


std::vector<Texture> _textures;

inline bool FileExists(const std::string& name) {
	struct stat buffer;
	return (stat(name.c_str(), &buffer) == 0);
}

void AssetManager::LoadFont() {
	_charExtents.clear();
	for (size_t i = 1; i <= 90; i++) {
		std::string filepath = "res/textures/font/char_" + std::to_string(i) + ".png";
		if (FileExists(filepath)) {
			Texture& texture = _textures.emplace_back(Texture(filepath));
			_charExtents.push_back({ texture.GetWidth(), texture.GetHeight()});
		}
	}
}

void AssetManager::LoadEverythingElse() {

	_textures.emplace_back(Texture("res/textures/CrosshairSquare.png"));
	_textures.emplace_back(Texture("res/textures/CrosshairDot.png"));
}

Texture& AssetManager::GetTexture(const std::string& filename) {
	for (Texture& texture : _textures) {
		if (texture.GetFilename() == filename)
			return texture;
	}
	std::cout << "Could not get texture with name \"" << filename << "\", it does not exist\n";
	Texture dummy;
	return dummy;
}