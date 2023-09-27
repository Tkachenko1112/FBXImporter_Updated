#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "FBXImporter.h"
#include <fstream>
#include <iostream>
#include <vector>
#include <map>
#include <algorithm>


int decodeIndex(int idx) {
	return (idx < 0) ? (-idx - 1) : idx;
}

std::string DataViewToString(ofbx::DataView data) {
	char out[128];
	data.toString(out);
	std::string result(out);
	return result;
}

glm::mat4 aiMatrix4x4ToGlm(const aiMatrix4x4& from)
{
	glm::mat4 to;
	//the a,b,c,d in assimp is the row ; the 1,2,3,4 is the column
	to[0][0] = from.a1; to[1][0] = from.a2; to[2][0] = from.a3; to[3][0] = from.a4;
	to[0][1] = from.b1; to[1][1] = from.b2; to[2][1] = from.b3; to[3][1] = from.b4;
	to[0][2] = from.c1; to[1][2] = from.c2; to[2][2] = from.c3; to[3][2] = from.c4;
	to[0][3] = from.d1; to[1][3] = from.d2; to[2][3] = from.d3; to[3][3] = from.d4;
	return to;
}

const char* CopyConstChar(const char* text)
{
	char* b = new char[strlen(text) + 1] {};
	std::copy(text, text + strlen(text), b);
	return b;
}

void FBXImporter::VertexBoneData::AddBoneData(unsigned int BoneID, float Weight)
{
	for (unsigned int i = 0; i < ARRAY_SIZE_IN_ELEMENTS(IDs); i++) {
		if (Weights[i] == 0.0) {
			IDs[i] = BoneID;
			Weights[i] = Weight;
			return;
		}
	}
	return;
	// should never get here - more bones than we have space for
	assert(0);
}


SkinnedModel* FBXImporter::LoadSkinnedModel(const char* filename)
{
	const aiScene* m_pScene;
	Assimp::Importer m_Importer;
	// m_Importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);

	SkinnedModel* skinnedModel = new SkinnedModel();

	skinnedModel->m_VAO = 0;
	ZERO_MEM(skinnedModel->m_Buffers);
	skinnedModel->m_NumBones = 0;
	skinnedModel->m_filename = filename;



	// Create the VAO
	glGenVertexArrays(1, &skinnedModel->m_VAO);
	glBindVertexArray(skinnedModel->m_VAO);

	// Create the buffers for the vertices attributes
	glGenBuffers(ARRAY_SIZE_IN_ELEMENTS(skinnedModel->m_Buffers), skinnedModel->m_Buffers);

	bool Ret = false;

	const aiScene* tempScene = m_Importer.ReadFile(filename, aiProcess_LimitBoneWeights | aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_FlipUVs);

	// aiProcess_OptimizeMeshes USE WITH BELOW 
	// aiProcess_OptimizeGraph 

	// aiProcess_PreTransformVertices

	//Getting corrupted later. So deep copying now.
	if (!tempScene) {
		std::cout << "something fucked up...\n";
		return nullptr;
	}

	m_pScene = new aiScene(*tempScene);

	if (m_pScene)
	{
		skinnedModel->m_GlobalInverseTransform = aiMatrix4x4ToGlm(m_pScene->mRootNode->mTransformation);
		skinnedModel->m_GlobalInverseTransform = glm::inverse(skinnedModel->m_GlobalInverseTransform);
		Ret = InitFromScene(skinnedModel, m_pScene, filename);
	}
	else {
		printf("Error parsing '%s': '%s'\n", filename, m_Importer.GetErrorString());
	}

	/* std::cout << "\nLoaded: " << filename << "\n";
	std::cout << " " << m_pScene->mNumMeshes << " meshes\n";
	std::cout << " " << skinnedModel->m_NumBones << " bones\n";

	for (int i = 0; i < skinnedModel->m_meshEntries.size(); i++)  {
		std::cout << " -" << skinnedModel->m_meshEntries[i].Name << ": " << skinnedModel->m_meshEntries[i].NumIndices << " indices " << skinnedModel->m_meshEntries[i].BaseIndex << " base index " << skinnedModel->m_meshEntries[i].BaseVertex << " base vertex\n";
	}*/





	if (m_pScene->mNumCameras > 0)
		aiCamera* m_camera = m_pScene->mCameras[0];

	FindBindPoseTransforms(skinnedModel, m_pScene->mRootNode); // only used for debugging at this point

	GrabSkeleton(skinnedModel, m_pScene->mRootNode, -1);


	std::cout << "Loaded model " << skinnedModel->m_filename << " (" << skinnedModel->m_BoneInfo.size() << " bones)\n";

	//std::cout << "m_GlobalInverseTransform\n";
	//Util::PrintMat4(skinnedModel->m_GlobalInverseTransform);

	skinnedModel->CalculateCameraBindposeTransform();

	for (auto b : skinnedModel->m_BoneInfo)
	{
		//std::cout << "-" << b.BoneName << "\n";
	}

	m_Importer.FreeScene();

	return skinnedModel;
}

bool FBXImporter::InitFromScene(SkinnedModel* skinnedModel, const aiScene* pScene, const std::string& Filename)
{
	skinnedModel->m_meshEntries.resize(pScene->mNumMeshes);

	std::vector<glm::vec3> Positions;
	std::vector<glm::vec3> Normals;
	std::vector<glm::vec2> TexCoords;
	std::vector<VertexBoneData> Bones;
	std::vector<unsigned int> Indices;

	unsigned int NumVertices = 0;
	unsigned int NumIndices = 0;

	// Count the number of vertices and indices
	for (unsigned int i = 0; i < skinnedModel->m_meshEntries.size(); i++)
	{
		skinnedModel->m_meshEntries[i].NumIndices = pScene->mMeshes[i]->mNumFaces * 3;
		skinnedModel->m_meshEntries[i].BaseVertex = NumVertices;
		skinnedModel->m_meshEntries[i].BaseIndex = NumIndices;
		skinnedModel->m_meshEntries[i].Name = pScene->mMeshes[i]->mName.C_Str();

		NumVertices += pScene->mMeshes[i]->mNumVertices;
		NumIndices += skinnedModel->m_meshEntries[i].NumIndices;

		std::cout << i+1 << "th Meah " << skinnedModel->m_meshEntries[i].Name << " was added." << std::endl;
	}

	// Reserve space in the vectors for the vertex attributes and indices
	Positions.reserve(NumVertices);
	Normals.reserve(NumVertices);
	TexCoords.reserve(NumVertices);
	Bones.resize(NumVertices);
	Indices.reserve(NumIndices);


	// Initialize the meshes in the scene one by one
	for (unsigned int i = 0; i < skinnedModel->m_meshEntries.size(); i++) {
		const aiMesh* paiMesh = pScene->mMeshes[i];
		InitMesh(skinnedModel, i, paiMesh, Positions, Normals, TexCoords, Bones, Indices);
	}


	glBindBuffer(GL_ARRAY_BUFFER, skinnedModel->m_Buffers[POS_VB]);
	glBufferData(GL_ARRAY_BUFFER, sizeof(Positions[0]) * Positions.size(), &Positions[0], GL_STATIC_DRAW);
	glEnableVertexAttribArray(POSITION_LOCATION);
	glVertexAttribPointer(POSITION_LOCATION, 3, GL_FLOAT, GL_FALSE, 0, 0);

	glBindBuffer(GL_ARRAY_BUFFER, skinnedModel->m_Buffers[NORMAL_VB]);
	glBufferData(GL_ARRAY_BUFFER, sizeof(Normals[0]) * Normals.size(), &Normals[0], GL_STATIC_DRAW);
	glEnableVertexAttribArray(NORMAL_LOCATION);
	glVertexAttribPointer(NORMAL_LOCATION, 3, GL_FLOAT, GL_FALSE, 0, 0);

	glBindBuffer(GL_ARRAY_BUFFER, skinnedModel->m_Buffers[TEXCOORD_VB]);
	glBufferData(GL_ARRAY_BUFFER, sizeof(TexCoords[0]) * TexCoords.size(), &TexCoords[0], GL_STATIC_DRAW);
	glEnableVertexAttribArray(TEX_COORD_LOCATION);
	glVertexAttribPointer(TEX_COORD_LOCATION, 2, GL_FLOAT, GL_FALSE, 0, 0);

	glBindBuffer(GL_ARRAY_BUFFER, skinnedModel->m_Buffers[TANGENT_VB]);
	glBufferData(GL_ARRAY_BUFFER, sizeof(Normals[0]) * Normals.size(), &Normals[0], GL_STATIC_DRAW);
	glEnableVertexAttribArray(TANGENT_LOCATION);
	glVertexAttribPointer(TANGENT_LOCATION, 3, GL_FLOAT, GL_FALSE, 0, 0);

	glBindBuffer(GL_ARRAY_BUFFER, skinnedModel->m_Buffers[BITANGENT_VB]);
	glBufferData(GL_ARRAY_BUFFER, sizeof(Normals[0]) * Normals.size(), &Normals[0], GL_STATIC_DRAW);
	glEnableVertexAttribArray(BITANGENT_LOCATION);
	glVertexAttribPointer(BITANGENT_LOCATION, 3, GL_FLOAT, GL_FALSE, 0, 0);

	glBindBuffer(GL_ARRAY_BUFFER, skinnedModel->m_Buffers[BONE_VB]);
	glBufferData(GL_ARRAY_BUFFER, sizeof(Bones[0]) * Bones.size(), &Bones[0], GL_STATIC_DRAW);
	glEnableVertexAttribArray(BONE_ID_LOCATION);
	glVertexAttribIPointer(BONE_ID_LOCATION, 4, GL_INT, sizeof(VertexBoneData), (const GLvoid*)0);
	glEnableVertexAttribArray(BONE_WEIGHT_LOCATION);
	glVertexAttribPointer(BONE_WEIGHT_LOCATION, 4, GL_FLOAT, GL_FALSE, sizeof(VertexBoneData), (const GLvoid*)16);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, skinnedModel->m_Buffers[INDEX_BUFFER]);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(Indices[0]) * Indices.size(), &Indices[0], GL_STATIC_DRAW);

	//std::cout << "INDICES.size: " << Indices.size() << "\n";

	return true;
}


void FBXImporter::InitMesh(SkinnedModel* skinnedModel, unsigned int MeshIndex,
	const aiMesh* paiMesh,
	std::vector<glm::vec3>& Positions,
	std::vector<glm::vec3>& Normals,
	std::vector<glm::vec2>& TexCoords,
	std::vector<VertexBoneData>& Bones,
	std::vector<unsigned int>& Indices)
{
	const aiVector3D Zero3D(0.0f, 0.0f, 0.0f);




	// Populate the vertex attribute vectors
	for (unsigned int i = 0; i < paiMesh->mNumVertices; i++) {
		const aiVector3D* pPos = &(paiMesh->mVertices[i]);
		const aiVector3D* pNormal = &(paiMesh->mNormals[i]);
		const aiVector3D* pTexCoord = paiMesh->HasTextureCoords(0) ? &(paiMesh->mTextureCoords[0][i]) : &Zero3D;

		Positions.push_back(glm::vec3(pPos->x, pPos->y, pPos->z));
		Normals.push_back(glm::vec3(pNormal->x, pNormal->y, pNormal->z));
		TexCoords.push_back(glm::vec2(pTexCoord->x, pTexCoord->y));


		// this is my shit. my own copy of the data. 
		// umm deal with this later. as in removing all reliance on assimp data structures..
		// Also keep in mind this is only half complete and doesn't have bone shit.
		// you are just using it to add the mesh to bullet for blood lol.

		Vertex v;
		v.position = Positions[i];
		v.normal = Normals[i];
		v.uv = TexCoords[i];
		//m_vertices.push_back(v);
	}

	
	LoadBones(skinnedModel, MeshIndex, paiMesh, Bones);

	// Populate the index buffer
	for (unsigned int i = 0; i < paiMesh->mNumFaces; i++) {
		const aiFace& Face = paiMesh->mFaces[i];
		assert(Face.mNumIndices == 3);
		Indices.push_back(Face.mIndices[0]);
		Indices.push_back(Face.mIndices[1]);
		Indices.push_back(Face.mIndices[2]);
	}
}



void FBXImporter::LoadBones(SkinnedModel* skinnedModel, unsigned int MeshIndex, const aiMesh* pMesh, std::vector<VertexBoneData>& Bones)
{
	for (unsigned int i = 0; i < pMesh->mNumBones; i++) {
		unsigned int BoneIndex = 0;
		std::string BoneName(pMesh->mBones[i]->mName.data);

		if (skinnedModel->m_BoneMapping.find(BoneName) == skinnedModel->m_BoneMapping.end()) {
			// Allocate an index for a new bone
			BoneIndex = skinnedModel->m_NumBones;
			skinnedModel->m_NumBones++;

			SkinnedModel::BoneInfo bi;
			skinnedModel->m_BoneInfo.push_back(bi);
			skinnedModel->m_BoneInfo[BoneIndex].BoneOffset = aiMatrix4x4ToGlm(pMesh->mBones[i]->mOffsetMatrix);
			skinnedModel->m_BoneInfo[BoneIndex].BoneName = BoneName;
			skinnedModel->m_BoneMapping[BoneName] = BoneIndex;

			std::cout << BoneIndex + 1 << "th Bone " << BoneName << " was added." << std::endl;
		}
		else {
			BoneIndex = skinnedModel->m_BoneMapping[BoneName];
		}

		for (unsigned int j = 0; j < pMesh->mBones[i]->mNumWeights; j++) {
			unsigned int VertexID = skinnedModel->m_meshEntries[MeshIndex].BaseVertex + pMesh->mBones[i]->mWeights[j].mVertexId;
			float Weight = pMesh->mBones[i]->mWeights[j].mWeight;
			Bones[VertexID].AddBoneData(BoneIndex, Weight);
		}
	}
}

void FBXImporter::GrabSkeleton(SkinnedModel* skinnedModel, const aiNode* pNode, int parentIndex)
{
	// Ok. So this function walks the node tree and makes a direct copy and that becomes your custom skeleton.
	// This includes camera nodes and all that fbx pre rotation/translation bullshit. Hopefully assimp will fix that one day.

	Joint joint;
	joint.m_name = CopyConstChar(pNode->mName.C_Str());
	joint.m_inverseBindTransform = aiMatrix4x4ToGlm(pNode->mTransformation);
	joint.m_parentIndex = parentIndex;


	//std::cout << "--" << joint.m_name << "\n";
   // Util::PrintMat4(joint.m_inverseBindTransform);


	parentIndex = skinnedModel->m_joints.size(); // don't do your head in with why this works, just be thankful it does.

	skinnedModel->m_joints.push_back(joint);


	/*std::string NodeName(pNode->mName.data);
	if (m_BoneMapping.find(NodeName) != m_BoneMapping.end()) {
		unsigned int BoneIndex = m_BoneMapping[NodeName];
		m_BoneInfo[BoneIndex].DebugMatrix_BindPose = inverse(m_BoneInfo[BoneIndex].BoneOffset);
	}*/

	for (unsigned int i = 0; i < pNode->mNumChildren; i++)
		GrabSkeleton(skinnedModel, pNode->mChildren[i], parentIndex);
}

void FBXImporter::FindBindPoseTransforms(SkinnedModel* skinnedModel, const aiNode* pNode)
{
	std::string NodeName(pNode->mName.data);

	if (skinnedModel->m_BoneMapping.find(NodeName) != skinnedModel->m_BoneMapping.end()) {
		unsigned int BoneIndex = skinnedModel->m_BoneMapping[NodeName];
		skinnedModel->m_BoneInfo[BoneIndex].DebugMatrix_BindPose = inverse(skinnedModel->m_BoneInfo[BoneIndex].BoneOffset);
	}

	for (unsigned int i = 0; i < pNode->mNumChildren; i++)
		FindBindPoseTransforms(skinnedModel, pNode->mChildren[i]);
}






void FBXImporter::LoadAnimation(SkinnedModel* skinnedModel, const char* Filename)
{
	aiScene* m_pAnimationScene;
	Assimp::Importer m_AnimationImporter;

	Animation* animation = new Animation(Filename);
	// m_filename = Filename;

	// Try and load the animation
	const aiScene* tempAnimScene = m_AnimationImporter.ReadFile(Filename, aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_FlipUVs);

	// Failed
	if (!tempAnimScene) {
		std::cout << "Could not load: " << Filename << "\n";
		delete animation;
		return;// assert(0);
	}

	// Success
	m_pAnimationScene = new aiScene(*tempAnimScene);
	if (m_pAnimationScene) {
		animation->m_duration = (float)m_pAnimationScene->mAnimations[0]->mDuration;
		animation->m_ticksPerSecond = m_pAnimationScene->mAnimations[0]->mTicksPerSecond;
		//std::cout << "Loaded animation: " << Filename << "\n";
	}


	// Some other error possibilty
	else {
		printf("Error parsing '%s': '%s'\n", Filename, m_AnimationImporter.GetErrorString());
		assert(0);
	}

	// need to create an animation clip.
	// need to fill it with animation poses.
	aiAnimation* aiAnim = m_pAnimationScene->mAnimations[0];


	//std::cout << " numChannels:" << aiAnim->mNumChannels << "\n";

	// so iterate over each channel, and each channel is for each NODE aka joint.

	// Resize the vecotr big enough for each pose
	int nodeCount = aiAnim->mNumChannels;
	int poseCount = aiAnim->mChannels[0]->mNumPositionKeys;

	// trying the assimp way now. coz why fight it.
	for (int n = 0; n < nodeCount; n++)
	{
		const char* nodeName = CopyConstChar(aiAnim->mChannels[n]->mNodeName.C_Str());

		AnimatedNode animatedNode(nodeName);
		animation->m_NodeMapping.emplace(nodeName, n);

		for (unsigned int p = 0; p < aiAnim->mChannels[n]->mNumPositionKeys; p++)
		{
			SQT sqt;
			aiVectorKey pos = aiAnim->mChannels[n]->mPositionKeys[p];
			aiQuatKey rot = aiAnim->mChannels[n]->mRotationKeys[p];
			aiVectorKey scale = aiAnim->mChannels[n]->mScalingKeys[p];

			sqt.positon = glm::vec3(pos.mValue.x, pos.mValue.y, pos.mValue.z);
			sqt.rotation = glm::quat(rot.mValue.w, rot.mValue.x, rot.mValue.y, rot.mValue.z);
			sqt.scale = scale.mValue.x;
			sqt.timeStamp = aiAnim->mChannels[n]->mPositionKeys[p].mTime;

			animation->m_finalTimeStamp = (((animation->m_finalTimeStamp) > (sqt.timeStamp)) ? (animation->m_finalTimeStamp) : (sqt.timeStamp));

			animatedNode.m_nodeKeys.push_back(sqt);
		}
		animation->m_animatedNodes.push_back(animatedNode);
	}

	// std::cout << animation->m_filename << " " << animation->m_duration << "\n";

	 // Store it
	skinnedModel->m_animations.emplace_back(animation);
	m_AnimationImporter.FreeScene();
}

void FBXImporter::LoadAllAnimations(SkinnedModel* skinnedModel, const char* Filename)
{

	aiScene* m_pAnimationScene;
	Assimp::Importer m_AnimationImporter;

	// m_filename = Filename;

	std::string filepath = Filename;

	// Try and load the animation
	const aiScene* tempAnimScene = m_AnimationImporter.ReadFile(filepath.c_str(), aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_FlipUVs);

	// Failed
	if (!tempAnimScene) {
		std::cout << "Could not load: " << Filename << "\n";
		assert(0);
	}

	// Success
	m_pAnimationScene = new aiScene(*tempAnimScene);
	if (m_pAnimationScene) {

		std::cout << "Loading animations: " << Filename << "\n";
		for (int i = 0; i < m_pAnimationScene->mNumAnimations; i++)
		{
			auto name = m_pAnimationScene->mAnimations[i]->mName.C_Str();
			Animation* animation = new Animation(name);

			animation->m_duration = (float)m_pAnimationScene->mAnimations[i]->mDuration;
			animation->m_ticksPerSecond = m_pAnimationScene->mAnimations[i]->mTicksPerSecond;
			std::cout << "Loaded " << i << ": " << name << "\n";

			auto a = m_pAnimationScene->mNumAnimations;

			// need to create an animation clip.
			// need to fill it with animation poses.
			aiAnimation* aiAnim = m_pAnimationScene->mAnimations[i];

			// so iterate over each channel, and each channel is for each NODE aka joint.
			// Resize the vecotr big enough for each pose
			int nodeCount = aiAnim->mNumChannels;
			int poseCount = aiAnim->mChannels[i]->mNumPositionKeys;

			// trying the assimp way now. coz why fight it.
			for (int n = 0; n < nodeCount; n++)
			{
				const char* nodeName = CopyConstChar(aiAnim->mChannels[n]->mNodeName.C_Str());

				AnimatedNode animatedNode(nodeName);
				animation->m_NodeMapping.emplace(nodeName, n);

				for (unsigned int p = 0; p < aiAnim->mChannels[n]->mNumPositionKeys; p++)
				{
					SQT sqt;
					aiVectorKey pos = aiAnim->mChannels[n]->mPositionKeys[p];
					aiQuatKey rot = aiAnim->mChannels[n]->mRotationKeys[p];
					aiVectorKey scale = aiAnim->mChannels[n]->mScalingKeys[p];

					sqt.positon = glm::vec3(pos.mValue.x, pos.mValue.y, pos.mValue.z);
					sqt.rotation = glm::quat(rot.mValue.w, rot.mValue.x, rot.mValue.y, rot.mValue.z);
					sqt.scale = scale.mValue.x;
					sqt.timeStamp = aiAnim->mChannels[n]->mPositionKeys[p].mTime;

					animation->m_finalTimeStamp = (((animation->m_finalTimeStamp) > (sqt.timeStamp)) ? (animation->m_finalTimeStamp) : (sqt.timeStamp));

					animatedNode.m_nodeKeys.push_back(sqt);
				}
				animation->m_animatedNodes.push_back(animatedNode);
				std::cout << i + 1 << "th Animation's " << n + 1 << "th AnimatedNode " << nodeName << " was added." << std::endl;
			}
			// Store it
			skinnedModel->m_animations.emplace_back(animation);
		}

	}


	// Some other error possibilty
	else {
		printf("Error parsing '%s': '%s'\n", Filename, m_AnimationImporter.GetErrorString());
		assert(0);
	}

	m_AnimationImporter.FreeScene();
}

SkinnedModel FBXImporter::LoadFile(std::string filepath) {

	SkinnedModel _skinnedModel = *LoadSkinnedModel(filepath.c_str());

	LoadAllAnimations(&_skinnedModel, filepath.c_str());

	return _skinnedModel;
	
	//FILE* fp = fopen(filepath.c_str(), "rb");
	//if (!fp) {
	//	std::cout << "Failed to open: " << filepath << "\n";
	//	return;
	//}
	//fseek(fp, 0, SEEK_END);
	//long file_size = ftell(fp);
	//fseek(fp, 0, SEEK_SET);
	//auto* content = new ofbx::u8[file_size];
	//fread(content, 1, file_size, fp);
	//ofbx::IScene*  scene = ofbx::load((ofbx::u8*)content, file_size, (ofbx::u64)ofbx::LoadFlags::TRIANGULATE);
	//delete[] content;
	//fclose(fp);

	//// Find all the bones
	//for (int k = 0; k < scene->getMeshCount(); k++)
	//{
	//	auto* fbxMesh = scene->getMesh(k);
	//	auto* geometry = fbxMesh->getGeometry();
	//	auto* skin = geometry->getSkin();

	//	// Bones
	//	if (skin) {
	//		for (int i = 0; i < skin->getClusterCount(); i++) {
	//			const auto* cluster = skin->getCluster(i);
	//			const ofbx::Object* link = cluster->getLink();
	//			if (link) {
	//				// Add bone if it doesn't already exist in the bones vector
	//				if (std::find(out->_bones.begin(), out->_bones.end(), link) != out->_bones.end()) {
	//				}
	//				else {
	//					std::cout << "Added bone: " << link->name << "\n";
	//					out->_bones.push_back(link);
	//				}
	//			}
	//		}
	//	}
	//	// Sort the bones into a flat array, so that no child is before its parent (required for skinning)
	//	for (int i = 0; i < out->_bones.size(); ++i) {
	//		for (int j = i + 1; j < out->_bones.size(); ++j) {
	//			if (out->_bones[i]->getParent() == out->_bones[j]) {
	//				const ofbx::Object* bone = out->_bones[j];
	//				out->_bones.erase(out->_bones.begin() + j);
	//				out->_bones.insert(out->_bones.begin() + i, bone);
	//				--i;
	//				break;
	//			}
	//		}
	//	}
	//	// Map bone names to their index
	//	for (int i = 0; i < out->_bones.size(); ++i) {
	//		out->_boneMapping[out->_bones[i]->name] = i;
	//	}
	//}


	//// Load vertex and index data
	//for (int i = 0; i < scene->getMeshCount(); i++)
	//{
	//	auto* fbxMesh = scene->getMesh(i);
	//	auto* geometry = fbxMesh->getGeometry();
	//	auto* skin = geometry->getSkin();
	//	auto* faceIndicies = geometry->getFaceIndices();
	//	auto indexCount = geometry->getIndexCount();

	//	// Get indices
	//	std::vector<unsigned int> indices;
	//	for (int q = 0; q < indexCount; q++) {
	//		int index = decodeIndex(faceIndicies[q]);
	//		indices.push_back(index);
	//	}

	//	// Get vertices
	//	std::vector<VertexWeighted> vertices;
	//	for (int j = 0; j < geometry->getVertexCount(); j++)
	//	{
	//		auto& v = geometry->getVertices()[j];
	//		auto& n = geometry->getNormals()[j];
	//		auto& t = geometry->getTangents()[j];
	//		auto& uv = geometry->getUVs()[j];

	//		VertexWeighted vertex;
	//		vertex.position = glm::vec3(v.x, v.y, v.z);
	//		vertex.normal = glm::vec3(n.x, n.y, n.z);

	//		if (geometry->getTangents() != nullptr) {
	//			vertex.tangent = glm::vec3(t.x, t.y, t.z);
	//			vertex.bitangent = glm::cross(vertex.tangent, vertex.normal);
	//		}
	//		if (geometry->getUVs() != nullptr)
	//			vertex.uv = glm::vec2(uv.x, 1.0f - uv.y);

	//		vertex.blendingIndex[0] = -1;
	//		vertex.blendingIndex[1] = -1;
	//		vertex.blendingIndex[2] = -1;
	//		vertex.blendingIndex[3] = -1;
	//		vertex.blendingWeight = glm::vec4(0, 0, 0, 0);
	//		vertices.push_back(vertex);
	//	}

	//	struct Weight {
	//		float influence;
	//		int boneID;
	//	};

	//	std::vector<std::vector<Weight>> vertexWeights;
	//	vertexWeights.resize(vertices.size());

	//	if (skin) {

	//		// Get vertex weights
	//		for (int i = 0; i < skin->getClusterCount(); i++)
	//		{
	//			const auto* cluster = skin->getCluster(i);
	//			const ofbx::Object* link = cluster->getLink();

	//			for (int j = 0; j < cluster->getIndicesCount(); ++j) {
	//				const int vertexIndex = cluster->getIndices()[j];
	//				const float influence = (float)cluster->getWeights()[j];
	//				const int boneIndex = out->_boneMapping[cluster->getLink()->name];
	//				vertexWeights[vertexIndex].push_back({ influence, boneIndex });
	//			}
	//		}
	//	}

	//	// Sort vertex weights by influence
	//	auto compare = [](Weight a, Weight b) {
	//		return (a.influence > b.influence);
	//	};

	//	for (int i = 0; i < vertexWeights.size(); i++) {
	//		std::sort(vertexWeights[i].begin(), vertexWeights[i].end(), compare);

	//		// Add the weight info to the actual main vertices
	//		for (int j = 0; j < 4 && j < vertexWeights[i].size(); j++) {
	//			vertices[i].blendingWeight[j] = vertexWeights[i][j].influence;
	//			vertices[i].blendingIndex[j] = vertexWeights[i][j].boneID;
	//		}
	//	}

	//	// Create the mesh, aka send the data to OpenGL
	//	out->_meshes.push_back(Mesh(vertices, indices, fbxMesh->name));



	//	////////////////////////////////////////////////////////////
	//	//														  //
	//	//	The code from here on is not working correctly !!!!!  //
	//	//														  //
	//	////////////////////////////////////////////////////////////

	//	
	//}



	//std::cout << "AnimationStackCount: " << scene->getAnimationStackCount() << " \n";

	//// Gather animations
	//for (int i = 0; i < scene->getAnimationStackCount(); ++i) {
	//	auto name = (ofbx::AnimationStack*)scene->getAnimationStack(i)->name;
	//	Animation* animation = new Animation((char*)name);

	//	animation->m_duration = (float)(ofbx::AnimationStack*)scene->getAnimationStack(i)->;
	//	animation->m_ticksPerSecond = m_pAnimationScene->mAnimations[i]->mTicksPerSecond;
	//	std::cout << "Loaded " << i << ": " << name << "\n";
	//	Animation& anim = out->_animations.emplace_back(Animation());

	//	const ofbx::AnimationStack* animationStack = (ofbx::AnimationStack*)scene->getAnimationStack(i);

	//	if (animationStack)
	//	{
	//		const ofbx::TakeInfo* takeInfo = scene->getTakeInfo(animationStack->name);

	//		if (takeInfo) {
	//			if (takeInfo->name.begin != takeInfo->name.end) {
	//				anim._name = DataViewToString(takeInfo->name);
	//			}
	//			if (anim._name.empty() && takeInfo->filename.begin != takeInfo->filename.end) {
	//				anim._name = DataViewToString(takeInfo->filename);
	//			}
	//			if (anim._name.empty()) anim._name = "anim";
	//		}
	//		else {
	//			anim._name = "";
	//			std::cout << "Warning: TakeInfo is NULL" << std::endl;
	//		}

	//		const ofbx::AnimationLayer* anim_layer = animationStack->getLayer(0);
	//		if (anim_layer)
	//		{
	//			bool data_found = false;
	//			for (int k = 0; anim_layer->getCurveNode(k); ++k) {
	//				const ofbx::AnimationCurveNode* node = anim_layer->getCurveNode(k);

	//				//adding code
	//				const std::string bonename = node->getBone()->name;

	//				Channel& channel = anim.channels.emplace_back(Channel());
	//				channel.boneName = bonename;
	//				
	//				std::cout << "Channel was added: " << bonename << std::endl;
	//				
	//				const ofbx::AnimationCurve* curveX = node->getCurve(0);
	//				const ofbx::AnimationCurve* curveY = node->getCurve(1);
	//				const ofbx::AnimationCurve* curveZ = node->getCurve(2);

	//				if (curveX && curveY && curveZ) {
	//					int keyCountX = curveX->getKeyCount();
	//					int keyCountY = curveY->getKeyCount();
	//					int keyCountZ = curveZ->getKeyCount();

	//					const int64_t* timeX = curveX->getKeyTime();
	//					const float* valueX = curveX->getKeyValue();
	//					const int64_t* timeY = curveY->getKeyTime();
	//					const float* valueY = curveY->getKeyValue();
	//					const int64_t* timeZ = curveZ->getKeyTime();
	//					const float* valueZ = curveZ->getKeyValue();

	//					// Check if all curves have the same number of keys
	//					if (keyCountX == keyCountY && keyCountY == keyCountZ) {
	//						for (int m = 0; m < keyCountX; m++) {
	//							KeyFrame keyframe;
	//							keyframe.time = timeX[m]; // Consider verifying if time is the same for all axes


	//							std::string tempName = node->name;
	//							if (tempName == "R") {
	//								data_found = true;
	//								keyframe.rotation.x = valueX[m];
	//								keyframe.rotation.y = valueY[m];
	//								keyframe.rotation.z = valueZ[m];
	//								keyframe.type = ROTATION;
	//								channel.keyFrames.push_back(keyframe);
	//								std::cout << "Added KeyFrame: " << keyframe.time << " - Rotation(" << keyframe.rotation.x << ", " << keyframe.rotation.y << ", " << keyframe.rotation.z << ") " << std::endl;

	//							}
	//							else if (tempName == "T") {
	//								data_found = true;
	//								keyframe.type = TRANSLATION;
	//								keyframe.translation.x = valueX[m];
	//								keyframe.translation.y = valueY[m];
	//								keyframe.translation.z = valueZ[m];
	//								channel.keyFrames.push_back(keyframe);
	//								std::cout << "Added KeyFrame: " << keyframe.time << " - TRANSLAION(" << keyframe.translation.x << ", " << keyframe.translation.y << ", " << keyframe.translation.z << ") " << std::endl;
	//							}
	//							else if (tempName == "S") {
	//								data_found = true;
	//								keyframe.type = SCALE;
	//								keyframe.scale.x = valueX[m];
	//								keyframe.scale.y = valueY[m];
	//								keyframe.scale.z = valueZ[m];
	//								channel.keyFrames.push_back(keyframe);
	//								std::cout << "Added KeyFrame: " << keyframe.time << " - SCALE(" << keyframe.scale.x << ", " << keyframe.scale.y << ", " << keyframe.scale.z << ") " << std::endl;
	//							}
	//							else {
	//								std::cout << "something is the fuck up, look here\n";
	//							}

	//						}
	//					}
	//					else {
	//						std::cout << "Warning: Mismatch in key counts across axes" << std::endl;
	//					}
	//				}
	//				else {
	//					std::cout << "Warning: Some curves are NULL" << std::endl;
	//				}

	//			}
	//			if (!data_found)
	//			{
	//				std::cout << "Warning: Animation data not found" << std::endl;
	//				out->_animations.pop_back();
	//			}
	//		}
	//		else
	//		{
	//			std::cout << "Warning: AnimationLayer is NULL" << std::endl;
	//			out->_animations.pop_back();
	//		}

	//	}
	//	else
	//	{
	//		std::cout << "Warning: AnimationStack is NULL" << std::endl;
	//		out->_animations.pop_back();
	//	}

	//}

	//if (out->_animations.size() == 1) {
	//	out->_animations[0]._name = "";
	//}

	//for (Animation& animation : out->_animations) {
	//	std::cout << "Animation found: " << animation._name << std::endl;
	//}

}
