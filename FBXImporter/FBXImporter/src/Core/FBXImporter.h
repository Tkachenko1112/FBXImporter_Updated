#pragma once

#include <string>
#include <map>
#include <vector>
#include "ofbx.h"
#include "../Renderer/Mesh.h"
#include "../Renderer/SkinnedModel.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

//struct SkinnedModel {
//	std::vector<Mesh> _meshes;
//	std::map<std::string, unsigned int> _boneMapping; 
//	std::vector<Animation> _animations;
//	std::vector<const ofbx::Object*> _bones;			// Ideally remove this obfx dependancy
//};

class FBXImporter {
	public:
		struct VertexBoneData
		{
			unsigned int IDs[4];
			float Weights[4];

			VertexBoneData()
			{
				Reset();
			};

			void Reset()
			{
				ZERO_MEM(IDs);
				ZERO_MEM(Weights);
			}

			void AddBoneData(unsigned int BoneID, float Weight);
		};

		static SkinnedModel LoadFile(std::string filepath);
		static SkinnedModel* LoadSkinnedModel(const char* filename);
		static bool InitFromScene(SkinnedModel* skinnedModel, const aiScene* pScene, const std::string& Filename);

		static void InitMesh(SkinnedModel* skinnedModel,
			unsigned int MeshIndex,
			const aiMesh* paiMesh,
			std::vector<glm::vec3>& Positions,
			std::vector<glm::vec3>& Normals,
			std::vector<glm::vec2>& TexCoords,
			std::vector<VertexBoneData>& Bones,
			std::vector<unsigned int>& Indices);

		static void LoadBones(SkinnedModel* skinnedModel, unsigned int MeshIndex, const aiMesh* paiMesh, std::vector<VertexBoneData>& Bones);

		static void GrabSkeleton(SkinnedModel* skinnedModel, const aiNode* pNode, int parentIndex); // does the same as below, but using my new abstraction stuff
		static void FindBindPoseTransforms(SkinnedModel* skinnedModel, const aiNode* pNode); // for debugging


		static void LoadAnimation(SkinnedModel* skinnedModel, const char* Filename);
		static void LoadAllAnimations(SkinnedModel* skinnedModel, const char* Filename);

};