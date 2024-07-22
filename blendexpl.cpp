#include <iostream>
#include <cassert>
#include <array>
#include <vector>
#include <optional>
#include <string>
#include <string_view>

// https://archive.blender.org/wiki/index.php/Dev:Source/Architecture/File_Format/
// https://github.com/blender/blender/tree/master/source/blender/makesdna
// http://homac.cakelab.org/projects/JavaBlend/spec.html
// https://devtalk.blender.org/t/best-way-to-create-a-mesh-object-in-c/3714/4

const std::string_view BLEND_FILE = "untitled.blend";

struct MemorySpan
{
	constexpr bool Empty() const { return (end - begin <= 0); }
	constexpr uint8_t* Data() { return begin; }
	constexpr uint8_t* Data() const { return begin; }
	constexpr size_t Size() const { return (end - begin); }

	void Advance(const size_t d = 1) { begin += d; }
	void Align4()
	{
		const size_t misAlign = reinterpret_cast<uint64_t>(begin) & 3;
		if(misAlign != 0)
			Advance(4 - misAlign);
	}

	const std::string_view AsString() const { return reinterpret_cast<const char*>(begin); }

	uint8_t* begin{ nullptr };
	uint8_t* end{ nullptr };
};

template<typename T>
T* ReadTypePtr(MemorySpan& span, size_t count = 1)
{
	T* val = reinterpret_cast<T*>(span.Data());
	span.Advance(sizeof(T) * count);
	return val;
}

template<typename T>
T* PeekTypePtr(MemorySpan span, size_t offset)
{
	span.Advance(offset);
	return reinterpret_cast<T*>(span.Data());
}

namespace blender
{
	using PtrType = uint64_t;

	enum class PointerSize
	{
		PTR_4,
		PTR_8
	};

	enum class Endianness
	{
		LittleEndian,
		BigEndian
	};

	struct FileHeader
	{
		uint8_t id[7];		// File identifier (always 'BLENDER')
		uint8_t pointerSize;// Size of a pointer; all pointers in the file are stored in this format. '_' means 4 bytes or 32 bit and '-' means 8 bytes or 64 bits.
		uint8_t endianness; // Type of byte ordering used; 'v' means little endian and 'V' means big endian.
		uint8_t version[3]; // Version of Blender the file was created in; '254' means version 2.54
	};

	struct FileBlockDesc64
	{
		uint8_t code[4];	// File-block identifier
		uint32_t size;		// Total length of the data after the file-block-header
		PtrType oldMemoryAddress; // Memory address the structure was located when written to disk
		uint32_t sdnaIndex;	// Index of the SDNA structure
		uint32_t count;		// Number of structure located in this file-block
	};

	struct FileBlock
	{
		FileBlockDesc64 desc;
		MemorySpan data;
		std::vector<FileBlock> childBlocks;
		std::ptrdiff_t fileOffset; //debug
	};

	inline const char HeaderID[7] = { 'B', 'L', 'E', 'N', 'D', 'E', 'R' };
	inline const char BlockSDNA[4] = { 'D', 'N', 'A', '1' };
	inline const char BlockOB[4] = { 'O', 'B', 0, 0 }; // object
	inline const char BlockME[4] = { 'M', 'E', 0, 0 }; // mesh
	inline const char BlockAR[4] = { 'A', 'R', 0, 0 }; // armature
	inline const char BlockSC[4] = { 'S', 'C', 0, 0 }; // scene
	inline const char BlockDATA[4] = { 'D', 'A', 'T', 'A' };
	inline const char EOFMark[4] = { 'E', 'N', 'D', 'B' };
	inline const char EOBMark[4] = { 'E', 'N', 'D', 'B' };

	inline constexpr size_t ID_NAME_LENGTH = 66;

	enum class OB_TYPE: int16_t
	{
		OB_MESH = 1,
		OB_ARMATURE = 25,
	};

	struct Link
	{
		PtrType next;
		PtrType prev;
	};

	struct ID
	{
		PtrType next;
		PtrType prev;
		ID* newid;
		void* library;
		uint8_t name[ID_NAME_LENGTH];
		uint16_t flag;
		int32_t tag;
		int32_t us;
		int32_t icon_id;
		int32_t recalc;
		int32_t recalc_up_to_undo_push;
		int32_t recalc_after_undo_push;
		int32_t session_uuid;
		void* properties;
		void* override_library;
		ID* orig_id;
		void* py_instance;
	};

	struct ListBase
	{
		PtrType first;
		PtrType last;
	};

	struct CollectionObject
	{
		CollectionObject *next, *prev;
		PtrType ob;
	};

	struct CollectionChild
	{
		CollectionChild *next, *prev;
		PtrType collection;
	};

	struct MVert
	{
		float co[3];
		int16_t no[3];
		char flag;
		char bweight;
	};

	struct MDeformWeight
	{
		int32_t def_nr; // The index for the vertex group, must *always* be unique when in an array.
		float weight;
	};

	struct MDeformVert
	{
		MDeformWeight* dw;
		int32_t totweight;
		int32_t flag;
	};

	struct MEdge
	{
		int32_t v1;
		int32_t v2;
		int8_t crease;
		int8_t bweight;
		int16_t flag;
	};

	struct MLoop
	{
		int32_t v;
		int32_t e;
	};

	struct MLoopUV
	{
		float uv[2];
		int32_t flag;
	};

	struct MLoopCol
	{
		uint8_t r;
		uint8_t g;
		uint8_t b;
		uint8_t a;
	};

	struct MPoly
	{
		int32_t loopstart;
		int32_t totloop;
		int16_t mat_nr;
		int8_t flag;
		int8_t _pad;
	};

	struct MDeformGroup
	{
		PtrType *next;
		PtrType *prev;
		uint8_t name[64];
		uint8_t flag;
		uint8_t _pad[7];
	};

	struct Float3
	{
		float x, y, z;
	};

	struct Float4
	{
		float w, x, y, z;
	};

	void NormalShortToFloat(float out[3], const int16_t in[3])
	{
		out[0] = in[0] * (1.0f / 32767.0f);
		out[1] = in[1] * (1.0f / 32767.0f);
		out[2] = in[2] * (1.0f / 32767.0f);
	}
}

class blendMesh
{
	public:
		friend class blendExpl;

	protected:
		void Read_MVert(MemorySpan span, size_t count) const
		{
			for(size_t i=0; i<count; ++i)
			{
				const auto* mvert = ReadTypePtr<blender::MVert>(span);
				std::cout << "Vertex#" << i << " coord (" << mvert->co[0] << ", " << mvert->co[1] << ", " << mvert->co[0] << ") ";

				blender::Float3 normalf;
				blender::NormalShortToFloat(&normalf.x, mvert->no);
				std::cout << "normal (" << normalf.x << ", " <<normalf.y << ", " << normalf.z << ")\n";
			}
		}

		void Read_MDeformVert(MemorySpan span, size_t count)
		{
			for(size_t i=0; i<count; ++i)
			{
				const auto* dvert = ReadTypePtr<blender::MDeformVert>(span);
				std::cout << "VertexGroup#" << i << " num_weights: " << dvert->totweight << '\n';
			}
		}

		void Read_MDeformWeight(MemorySpan span, size_t count)
		{
			for(size_t i=0; i<count; ++i)
			{
				const auto* dweight = ReadTypePtr<blender::MDeformWeight>(span);
				std::cout << "Weight#" << numWeights << "_" << i << " def_nr: " << dweight->def_nr << " w: " << ' ' << dweight->weight << '\n';
				numWeights++;
			}
		}

		void Read_MLoopUV(MemorySpan span, size_t count) const
		{
			for(size_t i=0; i<count; ++i)
			{
				const auto* mloop = ReadTypePtr<blender::MLoopUV>(span);
				std::cout << "LoopUV#" << i << " (" << mloop->uv[0] << ", " << mloop->uv[1] << ")\n";
			}
		}

		void Read_MLoop(MemorySpan span, size_t count) const
		{
			for(size_t i=0; i<count; ++i)
			{
				const auto* mloop = ReadTypePtr<blender::MLoop>(span);
				std::cout << "Loop#" << i << " v: " << mloop->v << " e: " << mloop->e << '\n';
			}
		}

		void Read_MLoopCol(MemorySpan span, size_t count) const
		{
			for(size_t i=0; i<count; ++i)
			{
				const auto* mcol = ReadTypePtr<blender::MLoopCol>(span);
				//std::cout << "Color# " << i << " (" << mcol->r << ',' << mcol->g << ',' << mcol->b << ',' << mcol->a << ")\n";
			}
		}

		void Read_MEdge(MemorySpan span, size_t count) const
		{
			for(size_t i=0; i<count; ++i)
			{
				const auto* edge = ReadTypePtr<blender::MEdge>(span);
				std::cout << "Edge#" << i << " (" << edge->v1 << ", " << edge->v2 << ")\n";
			}
		}

		void Read_MPoly(MemorySpan span, size_t count) const
		{
			for(size_t i=0; i<count; ++i)
			{
				const auto* poly = ReadTypePtr<blender::MPoly>(span);
				std::cout << "Poly#" << i << " loopstart: " << poly->loopstart << " totloop: " << poly->totloop << '\n';
			}
		}

		size_t numWeights{ 0 };
};

/*
* Traverse:
*	- Scene:
*		- Time Markers (for animation identification eg. 'enemy_run' frames[20,50])
*		- Master Collection:
*			- Objects
*				- Armature
*					- Bones
*						- BonePose
*				- Mesh
*					- Verts/Edges/Loops/Weights
*				- Key (ShapeKeys for vertex animations)
*/

class blendExpl
{
	public:
		~blendExpl()
		{
			Cleanup();
		}

		void Explore()
		{
			if(ParseFile(BLEND_FILE))
			{
				std::cout << '\n';

				blendMesh mesh;
				//ExploreMeshData(mesh);

				//PrintStructByName("Object");
				//ExploreNonDataBlocks();
				//ExploreDataBlocks();
				//ExploreObjectData();
				//ExploreScene();
				ExploreArmature();
			}
		}

		void ExploreNonDataBlocks()
		{
			for(const auto& block: m_blockArray)
			{
				if(Identify(block.desc.code, "DATA", 4))
					continue;

				PrintBlockSDNA(block);
				PrintStructBySDNA(block.desc.sdnaIndex);
			}
		}

		void ExploreDataBlocks()
		{
			size_t fcurves = 0;
			size_t actiongrps = 0;
			size_t beztriples = 0;

			for(const auto& block: m_blockArray)
			{
				if(!Identify(block.desc.code, "DATA", 4))
					continue;

				if(IdentifyStruct(block.desc.sdnaIndex, "FCurve"))
				{
					const uint32_t totvert = *PeekTypePtr<char>(block.data, GetFieldOffset("FCurve", "totvert"));
					std::cout << "FCurve totvert: " << totvert << '\n';
					fcurves++;
				}
				else if(IdentifyStruct(block.desc.sdnaIndex, "bActionGroup"))
				{
					const std::string_view name(PeekTypePtr<char>(block.data, GetFieldOffset("bActionGroup", "name[64]")));
					std::cout << "Action group name: " << name << '\n';

					actiongrps++;
				}
				else if(IdentifyStruct(block.desc.sdnaIndex, "BezTriple"))
				{
					const size_t bezTripleSize = GetStructSizeByName("BezTriple");
					MemorySpan bezTripleArraySpan = block.data;

					for(size_t i=0; i<2; ++i)
					{
						const float* vecPtr = PeekTypePtr<float>(bezTripleArraySpan, GetFieldOffset("BezTriple", "vec[3][3]"));
					
						std::cout << "Keyframe: " << std::nearbyint(vecPtr[3]) << "  [";
						for(size_t i=0; i<9; ++i)
							std::cout << vecPtr[i] << (i == 8 ? "]\n" : ",");

						beztriples++;
						bezTripleArraySpan.Advance(bezTripleSize);
					}
				}
			}

			std::cout << "FCurves: " << fcurves << '\n';
			std::cout << "bActionGroups: " << actiongrps << '\n';
			std::cout << "BezTriple: " << beztriples << '\n';
		}

		void ExploreObjectData()
		{
			const size_t offsetOfType = GetFieldOffset("Object", "type");
			const size_t offsetOfData = GetFieldOffset("Object", "*data");

			size_t prevFoundBlockId = -1;
			bool hasMoreObject = true;

			while(hasMoreObject)
			{
				const auto blockId = FindBlockByCode(blender::BlockOB, prevFoundBlockId + 1);
				hasMoreObject = blockId.has_value();

				if(hasMoreObject)
				{
					const size_t objectBlockId = blockId.value();
					const auto& block = m_blockArray.at(objectBlockId);

					std::cout << "Object name: " << GetBlockNameByID(block, true) << '\n';

					const blender::OB_TYPE type = *PeekTypePtr<blender::OB_TYPE>(block.data, offsetOfType);
					std::cout << "  Type: " << static_cast<size_t>(type) << '\n';

					const auto adtArmatureOb = PeekTypePtr<blender::PtrType>(block.data, GetFieldOffset("Object", "*adt"));
					if(adtArmatureOb != 0)
						std::cout << "Found animation data for object\n";

					if(type == blender::OB_TYPE::OB_MESH)
					{
						//std::cout << "----\n";
						//PrintBlockSDNA(block);
						//PrintStrucyBySDNA(block.blockDesc.sdnaIndex);

						size_t nextBlock = objectBlockId + 1;
						while(nextBlock < m_blockArray.size())
						{
							const auto& dataFileBlock = m_blockArray.at(nextBlock);
							if(!Identify(dataFileBlock.desc.code, "DATA", 4))
								break;

							const blender::FileBlockDesc64& blockDesc = dataFileBlock.desc;

							if(IdentifyStruct(blockDesc.sdnaIndex, "bDeformGroup"))
							{
								MemorySpan dgroupSpan = dataFileBlock.data;
								const auto* defGroup = ReadTypePtr<blender::MDeformGroup>(dgroupSpan);
								int d = 3;
							}

							nextBlock++;
						}
					}

					prevFoundBlockId = blockId.value();
				}
			}
		}

		void ExploreScene()
		{
			for(const auto& sceneBlock: m_blockArray)
			{
				if(!Identify(sceneBlock.desc.code, blender::BlockSC, 4))
					continue;

				std::cout << "Scene name: " << GetBlockNameByID(sceneBlock, true) << '\n';

				//PrintBlockSDNA(sceneBlock);
				//PrintStrucyBySDNA(sceneBlock.blockDesc.sdnaIndex);

				const auto renderDataOff = GetFieldOffset("Scene", "r");
				const auto sfra = *PeekTypePtr<int32_t>(sceneBlock.data, renderDataOff + GetFieldOffset("RenderData", "sfra"));
				const auto efra = *PeekTypePtr<int32_t>(sceneBlock.data, renderDataOff + GetFieldOffset("RenderData", "efra"));
				
				std::cout << "Frame range: " << sfra << '-' << efra << '\n';

				const auto collectionAddr = *PeekTypePtr<blender::PtrType>(sceneBlock.data, GetFieldOffset("Scene", "*master_collection"));

				for(const auto& collectionBlock: sceneBlock.childBlocks)
				{
					if(collectionBlock.desc.oldMemoryAddress == collectionAddr)
					{
						TraverseCollections(collectionBlock);
						break;
					}
				}

				for(const auto& childBlock: sceneBlock.childBlocks)
				{
					if(IdentifyStruct(childBlock.desc.sdnaIndex, "TimeMarker"))
					{
						const auto frame = *PeekTypePtr<int32_t>(childBlock.data, GetFieldOffset("TimeMarker", "frame"));
						std::string_view name(PeekTypePtr<char>(childBlock.data, GetFieldOffset("TimeMarker", "name[64]")));
						std::cout << "Found a time marker: " << name << " frame: " << frame << '\n';
					}
				}
			}
		}

		void TraverseCollections(const blender::FileBlock& collectionBlock)
		{
			assert(IdentifyStruct(collectionBlock.desc.sdnaIndex, "Collection"));

			//PrintBlockSDNA(collectionBlock);
			//PrintStrucyBySDNA(collectionBlock.blockDesc.sdnaIndex);

			std::cout << "Collection name: " << GetBlockNameByID(collectionBlock, true) << '\n';

			const auto* gobjectBase = PeekTypePtr<blender::ListBase>(collectionBlock.data, GetFieldOffset("Collection", "gobject"));
			blender::PtrType gobjectAddr = gobjectBase->first;

			if(gobjectAddr != 0)
				TraverseCollectionObjects(gobjectAddr);
			
			const auto* nextChild = PeekTypePtr<blender::ListBase>(collectionBlock.data, GetFieldOffset("Collection", "children"));
			if(nextChild->first != 0)
			{
				const auto optCollectionChild = FindFileBlockByOldAddr(nextChild->first);
				if(optCollectionChild.has_value())
				{
					const auto& collectionChild = optCollectionChild.value();
					const auto collectionPtr = *PeekTypePtr<blender::PtrType>(collectionChild.data, GetFieldOffset("CollectionChild", "*collection"));
					const auto& optCollection = FindFileBlockByOldAddr(collectionPtr);
					if(optCollection.has_value())
						TraverseCollections(optCollection.value());
				}
			}
		}

		void TraverseCollectionObjects(const blender::PtrType addr)
		{
			const auto collectionObjectOpt = FindFileBlockByOldAddr(addr);
			if(!collectionObjectOpt.has_value())
				return;

			const blender::FileBlock& collectionObject = collectionObjectOpt.value();
			assert(IdentifyStruct(collectionObject.desc.sdnaIndex, "CollectionObject"));

			const auto obAddr = *PeekTypePtr<blender::PtrType>(collectionObject.data, GetFieldOffset("CollectionObject", "*ob"));
			const auto obOpt = FindFileBlockByOldAddr(obAddr);
			if(obOpt.has_value())
			{
				const auto& ob = obOpt.value();
				std::cout << "  Object name: " << GetBlockNameByID(ob, true) << '\n';
			}

			const blender::PtrType nextAddr = *PeekTypePtr<blender::PtrType>(collectionObject.data, GetFieldOffset("CollectionObject", "*next"));
			if(nextAddr != 0)
				TraverseCollectionObjects(nextAddr);
		}

		void ExploreArmature()
		{
			const auto foundBlock = FindBlockByCode(blender::BlockAR, 0);
			if(foundBlock.has_value())
			{
				const size_t armatureBlockId = foundBlock.value();
				const auto& block = m_blockArray.at(armatureBlockId);

				std::cout << "Found armature block!\n";

				//PrintBlockSDNA(block);
				//PrintStrucyBySDNA(block.blockDesc.sdnaIndex);

				const auto parentObject = FindParentObject(block.desc.oldMemoryAddress);
				if(parentObject.has_value())
				{
					const auto& obBlock = parentObject.value();
					std::cout << "Parent object name: " << GetBlockNameByID(obBlock, true) << '\n';
				
					const auto adtArmatureObAddr = *PeekTypePtr<blender::PtrType>(obBlock.data, GetFieldOffset("Object", "*adt"));
					if(adtArmatureObAddr != 0)
					{
						const auto adtArmature = FindFileBlockByOldAddr(adtArmatureObAddr);
						assert(adtArmature.has_value());
						ExploreAnimationData(adtArmature.value());
					}
				}

				size_t numBonesForArmature = 0;

				size_t nextBlock = armatureBlockId + 1;
				while(nextBlock < m_blockArray.size())
				{
					const auto& dataFileBlock = m_blockArray.at(nextBlock);
					if(!Identify(dataFileBlock.desc.code, "DATA", 4))
						break;

					if(GetStructNameBySDNA(dataFileBlock.desc.sdnaIndex) == "Bone")
					{
						ExploreBone(dataFileBlock);
						numBonesForArmature++;
					}

					nextBlock++;
				}

				std::cout << "Number of bones in armature: " << numBonesForArmature << '\n';

				if(parentObject.has_value())
				{
					const auto& obBlock = parentObject.value();
					const auto poseAddr = *PeekTypePtr<blender::PtrType>(obBlock.data, GetFieldOffset("Object", "*pose"));
					const auto poseBlockOpt = FindFileBlockByOldAddr(poseAddr);
					if(poseBlockOpt.has_value())
						ExplorePose(poseBlockOpt.value());
				}
			}
		}

		void ExploreAnimationData(const blender::FileBlock& adt)
		{
			const auto adtActionPtr = *PeekTypePtr<blender::PtrType>(adt.data, GetFieldOffset("AnimData", "*action"));
			const auto adtAction = FindFileBlockByOldAddr(adtActionPtr);
			assert(adtAction.has_value());

			// fcurves of bAnimation

		}

		void ExploreBone(const blender::FileBlock& boneBlock)
		{
			std::cout << "--------------\n";
			const std::string_view nameView(PeekTypePtr<char>(boneBlock.data, GetFieldOffset("Bone", "name[64]")));
			std::cout << "Bone name: " << nameView << " parent: ";

			const auto boneParentAddr = *PeekTypePtr<blender::PtrType>(boneBlock.data, GetFieldOffset("Bone", "*parent"));
			if(boneParentAddr != 0)
			{
				const auto parentBoneOpt = FindFileBlockByOldAddr(boneParentAddr);
				assert(parentBoneOpt.has_value());

				const std::string_view nameView(PeekTypePtr<char>(parentBoneOpt.value().data, GetFieldOffset("Bone", "name[64]")));
				std::cout << nameView << '\n';
			}
			else
			{
				std::cout << "null\n";
			}

			if(false)
			{
				const auto* armMatBase = PeekTypePtr<float>(boneBlock.data, GetFieldOffset("Bone", "arm_mat[4][4]"));
				std::cout << "Bone armature matrix: [...]\n";
			
				for(size_t i=0; i<16; ++i)
					std::cout << armMatBase[i] << (i == 15 ? "]\n" : ", ");
			}
		}

		void ExplorePose(const blender::FileBlock& poseBlock)
		{
			//bPose, bPoseChannel
			const auto* posechan = PeekTypePtr<blender::ListBase>(poseBlock.data, GetFieldOffset("bPose", "chanbase"));
			if(posechan->first != 0)
				TraversePoseChannels(posechan->first);
		}

		void TraversePoseChannels(const blender::PtrType poseChanAddr)
		{
			const auto optPoseChannel = FindFileBlockByOldAddr(poseChanAddr);
			if(!optPoseChannel.has_value())
				return;

			const auto& poseChannel = optPoseChannel.value();
			assert(IdentifyStruct(poseChannel.desc.sdnaIndex, "bPoseChannel"));

			ExplorePoseChannel(poseChannel);

			const blender::PtrType nextAddr = *PeekTypePtr<blender::PtrType>(poseChannel.data, GetFieldOffset("bPoseChannel", "*next"));
			if(nextAddr != 0)
				TraversePoseChannels(nextAddr);
		}

		void ExplorePoseChannel(const blender::FileBlock& poseChannel)
		{
			std::cout << "--------------\n";
			const std::string_view chanNameView(PeekTypePtr<char>(poseChannel.data, GetFieldOffset("bPoseChannel", "name[64]")));
			std::cout << "Found a bPoseChannel: " << chanNameView << '\n';

			const auto chanBoneAddr = *PeekTypePtr<blender::PtrType>(poseChannel.data, GetFieldOffset("bPoseChannel", "*bone"));
			const auto chanBone = FindFileBlockByOldAddr(chanBoneAddr);
			assert(chanBone.has_value());

			const std::string_view boneNameView(PeekTypePtr<char>(chanBone.value().data, GetFieldOffset("Bone", "name[64]")));
			std::cout << "Channel bone name: " << boneNameView << '\n';

			if(true)
			{
				const auto* chanMatBase = PeekTypePtr<float>(poseChannel.data, GetFieldOffset("bPoseChannel", "chan_mat[4][4]"));
				std::cout << "Channel matrix: \n  [";
			
				for(size_t i=0; i<16; ++i)
					std::cout << chanMatBase[i] << (i == 15 ? "]\n" : ", ");
			}
		}

		void ExploreMeshData(blendMesh& mesh)
		{
			const auto foundBlock = FindBlockByCode(blender::BlockME, 0);
			if(foundBlock.has_value())
			{
				const size_t meshBlockId = foundBlock.value();
				const auto& block = m_blockArray.at(meshBlockId);

				std::cout << "Mesh name: " << GetBlockNameByID(block, true) << '\n';

				const auto totvert = *PeekTypePtr<uint32_t>(block.data, GetFieldOffset("Mesh", "totvert"));
				const auto totpoly = *PeekTypePtr<uint32_t>(block.data, GetFieldOffset("Mesh", "totpoly"));
				const auto totloop = *PeekTypePtr<uint32_t>(block.data, GetFieldOffset("Mesh", "totloop"));

				std::cout << "Verts: " << totvert << " polys: " << totpoly << " loops: " << totloop << '\n';
				std::cout << '\n';

				const auto parentObject = FindParentObject(block.desc.oldMemoryAddress);
				if(parentObject.has_value())
				{
					const auto& obBlock = parentObject.value();
					std::cout << "Object name: " << GetBlockNameByID(obBlock, true) << '\n';

					const auto loc = *PeekTypePtr<blender::Float3>(obBlock.data, GetFieldOffset("Object", "loc[3]"));
					const auto scale = *PeekTypePtr<blender::Float3>(obBlock.data, GetFieldOffset("Object", "size[3]"));
					const auto quat = *PeekTypePtr<blender::Float4>(obBlock.data, GetFieldOffset("Object", "quat[4]"));

					std::cout << "Translation x: " << loc.x << " y: " << loc.y << " z: " << loc.z << '\n';
					std::cout << "Scale x: " << scale.x << " y: " << scale.y << " z: " << scale.z << '\n';
					std::cout << "Rotation (quat) w: " << quat.w << " x: "  << quat.x << " y: " << quat.y << " z: " << quat.z << '\n';

					for(const auto& childBlock: obBlock.childBlocks)
					{
						if(IdentifyStruct(childBlock.desc.sdnaIndex, "ArmatureModifierData"))
						{
							const auto arModObject = *PeekTypePtr<blender::PtrType>(childBlock.data, GetFieldOffset("ArmatureModifierData", "*object"));
							const auto armatureParentObject = FindFileBlockByOldAddr(arModObject);
							if(armatureParentObject.has_value())
							{
								const auto& armatureObBlock = armatureParentObject.value();
								std::cout << "Armature object name: " << GetBlockNameByID(armatureObBlock, true) << '\n';
							}
						}
					}

					std::cout << '\n';
				}

				PrintBlockSDNA(block);
				PrintStructBySDNA(block.desc.sdnaIndex);

				size_t nextBlock = meshBlockId + 1;
				while(nextBlock < m_blockArray.size())
				{
					const auto& dataFileBlock = m_blockArray.at(nextBlock);
					if(!Identify(dataFileBlock.desc.code, "DATA", 4))
						break;

					if(false)
					{
						std::cout << "----\n";
						PrintBlockSDNA(dataFileBlock);
						PrintStructBySDNA(dataFileBlock.desc.sdnaIndex);
					}
					else
					{
						const blender::FileBlockDesc64& blockDesc = dataFileBlock.desc;
						const MemorySpan dataSpan = dataFileBlock.data;

						if(IdentifyStruct(blockDesc.sdnaIndex, "MVert"))
							mesh.Read_MVert(dataSpan, blockDesc.count);
						else if(IdentifyStruct(blockDesc.sdnaIndex, "MDeformVert"))
							mesh.Read_MDeformVert(dataSpan, blockDesc.count);
						else if(IdentifyStruct(blockDesc.sdnaIndex, "MDeformWeight"))
							mesh.Read_MDeformWeight(dataSpan, blockDesc.count);
						else if(IdentifyStruct(blockDesc.sdnaIndex, "MLoop"))
							mesh.Read_MLoop(dataSpan, blockDesc.count);
						else if(IdentifyStruct(blockDesc.sdnaIndex, "MLoopUV"))
							mesh.Read_MLoopUV(dataSpan, blockDesc.count);
						else if(IdentifyStruct(blockDesc.sdnaIndex, "MLoopCol"))
							mesh.Read_MLoopCol(dataSpan, blockDesc.count);
						else if(IdentifyStruct(blockDesc.sdnaIndex, "MEdge"))
							mesh.Read_MEdge(dataSpan, blockDesc.count);
						else if(IdentifyStruct(blockDesc.sdnaIndex, "MPoly"))
							mesh.Read_MPoly(dataSpan, blockDesc.count);
					}

					nextBlock++;
				}
			}		
		}

	private:
		struct StructDesc;

		bool ParseFile(std::string_view file)
		{
			Cleanup();

			FILE* f = nullptr;
			if(fopen_s(&f, file.data(), "rb") == 0 && f != nullptr)
			{
				fseek(f, 0, SEEK_END);
				const size_t fileLen = ftell(f);
				uint8_t* fileContent = new uint8_t[fileLen];

				fseek(f, 0, SEEK_SET);
				if(fread(fileContent, sizeof(uint8_t), fileLen, f) == fileLen)
					m_fileSpan = MemorySpan{ fileContent, fileContent + fileLen };

				fclose(f);
			}

			if(m_fileSpan.Empty())
			{
				std::cout << "File not found!\n";
				return false;
			}

			MemorySpan memoryStream = m_fileSpan;
			const auto* blendHeader = ReadTypePtr<blender::FileHeader>(memoryStream);

			if(memcmp(blendHeader->id, blender::HeaderID, sizeof(blender::HeaderID)) != 0)
			{
				std::cout << "ERROR - file header magic mismatch!\n";
				return false;
			}

			assert(blendHeader->pointerSize == '-' || blendHeader->pointerSize == '_');
			const blender::PointerSize ptrSize = (blendHeader->pointerSize == '_' ? blender::PointerSize::PTR_4 : blender::PointerSize::PTR_8);
			const blender::Endianness endian = (blendHeader->endianness == 'v' ? blender::Endianness::LittleEndian : blender::Endianness::BigEndian);

			if(ptrSize != blender::PointerSize::PTR_8 || endian != blender::Endianness::LittleEndian)
			{
				std::cout << "ERROR - this parser supports only 64bit, little endian blend files!\n";
				return false;
			}

			std::cout << "Blender version: " << std::string_view(reinterpret_cast<const char*>(blendHeader->version), 3) << " - ptr size 8, little-endian.\n";

			size_t blockCount = 0;
			size_t parentId = -1;

			while(!memoryStream.Empty())
			{
				auto* blendBlock = ReadTypePtr<blender::FileBlockDesc64>(memoryStream);

				blender::FileBlock block;
				block.desc = *blendBlock;
				block.data = MemorySpan{ memoryStream.begin, memoryStream.begin + blendBlock->size };
				block.fileOffset = reinterpret_cast<uint8_t*>(blendBlock) - m_fileSpan.begin;
				m_blockArray.emplace_back(block);

				if(Identify(blendBlock->code, blender::BlockDATA, 4))
				{
					assert(parentId != -1);
					m_blockArray.at(parentId).childBlocks.emplace_back(block);
				}
				else
				{
					if(Identify(blendBlock->code, blender::BlockSDNA, 4))
						ParseSDNA(blendBlock, memoryStream);
					else if(Identify(blendBlock->code, blender::EOBMark, 4))
						break;

					parentId = m_blockArray.size() - 1;
				}

				memoryStream.Advance(blendBlock->size);
				memoryStream.Align4();
				blockCount++;
			}

			std::cout << "End of parsing.\n";
			return true;
		}

		void ParseSDNA(blender::FileBlockDesc64* block, MemorySpan blockSpan)
		{
			const size_t sdnaDataSize = block->size;
			std::cout << "DNA1 block begin - size: " << block->size << '\n';

			//sdna block header
			const auto* sdnaHeaderId = ReadTypePtr<uint8_t>(blockSpan, 4); // 'SDNA'
			assert(Identify(sdnaHeaderId, "SDNA", 4));

			//read names array
			{
				const auto* sdnaName = ReadTypePtr<uint8_t>(blockSpan, 4); // 'NAME'
				assert(Identify(sdnaName, "NAME", 4));
				const uint32_t nameCount = *ReadTypePtr<uint32_t>(blockSpan);

				m_nameArray.reserve(nameCount);

				for(size_t i=0; i<nameCount; ++i)
				{
					MemorySpan nameSpan = { blockSpan.begin };

					while(!blockSpan.Empty() && *blockSpan.Data() != 0)
						blockSpan.Advance();

					blockSpan.Advance(); //string terminating 0
					nameSpan.end = blockSpan.Data();

					m_nameArray.emplace_back(nameSpan);
				}
			}

			uint32_t typeCount = 0;

			//read types array
			{
				blockSpan.Align4();
				const auto* sdnaTypes = ReadTypePtr<uint8_t>(blockSpan, 4); // 'TYPE'
				assert(Identify(sdnaTypes, "TYPE", 4));
				typeCount = *ReadTypePtr<uint32_t>(blockSpan);

				m_typeArray.reserve(typeCount);

				for(size_t i=0; i<typeCount; ++i)
				{
					MemorySpan typeSpan = { blockSpan.begin };

					while(!blockSpan.Empty() && *blockSpan.Data() != 0)
						blockSpan.Advance();

					blockSpan.Advance(); // string terminating 0
					typeSpan.end = blockSpan.Data();

					m_typeArray.emplace_back(TypeInfo{ typeSpan, 0 });
				}
			}

			//read lengths array
			{
				blockSpan.Align4();
				const auto* sdnaLengths = ReadTypePtr<uint8_t>(blockSpan, 4); // 'TLEN'
				assert(Identify(sdnaLengths, "TLEN", 4));

				for(size_t i=0; i<typeCount; ++i)
				{
					const uint16_t len = *ReadTypePtr<uint16_t>(blockSpan);
					m_typeArray.at(i).length = len;
				}
			}

			//read structures array
			{
				blockSpan.Align4();
				const auto* sdnaStructs = ReadTypePtr<uint8_t>(blockSpan, 4); // 'STRC'
				assert(Identify(sdnaStructs, "STRC", 4));
				const uint32_t structCount = *ReadTypePtr<uint32_t>(blockSpan);
				
				m_structArray.resize(structCount);

				for(size_t i=0; i<structCount; ++i)
				{
					StructDesc& structDesc = m_structArray.at(i);
					structDesc.typeIndex = *ReadTypePtr<uint16_t>(blockSpan);
					
					const uint16_t numFields = *ReadTypePtr<uint16_t>(blockSpan);
					structDesc.fields.reserve(numFields);

					for(uint16_t f=0; f<numFields; ++f)
					{
						const uint16_t fieldTypeIdx = *ReadTypePtr<uint16_t>(blockSpan);
						const uint16_t fieldNameIdx = *ReadTypePtr<uint16_t>(blockSpan);
						structDesc.fields.emplace_back(FieldDesc{ fieldTypeIdx, fieldNameIdx });
					}
				}
			}

			std::cout << "DNA1 block end.\n";
		}

		std::string_view GetUserName(const std::string_view name)
		{
			const size_t offset = (name.starts_with("ME") ? 2 : 0);
			const size_t firstZero = name.find_first_of('\0', offset);
			return std::string_view(name.data() + offset, firstZero - offset);
		}

		std::optional<size_t> FindBlockByCode(const char* code, size_t offset) const
		{
			for(size_t i=offset; i<m_blockArray.size(); ++i)
			{
				if(Identify(m_blockArray.at(i).desc.code, code, 4))
					return { i };
			}

			return {};
		}

		std::optional<blender::FileBlock> FindFileBlockByOldAddr(blender::PtrType oldAddressOfBlock) const
		{
			if(oldAddressOfBlock != 0)
			{
				for(const auto& block: m_blockArray)
				{
					if(block.desc.oldMemoryAddress == oldAddressOfBlock)
						return { block };
				}
			}

			return {};
		}

		size_t GetFieldOffset(const std::string_view sname, const std::string_view fname) const
		{
			size_t offset = 0;
			for(size_t i=0; i<m_structArray.size(); ++i)
			{
				const StructDesc& structDesc = m_structArray.at(i);
				const TypeInfo& structTypeInfo = m_typeArray.at(structDesc.typeIndex);
				if(sname == structTypeInfo.type.AsString())
				{
					for(const auto& field: structDesc.fields)
					{
						const std::string_view fieldName = m_nameArray.at(field.nameIndex).AsString();
						if(fname == fieldName)
							break;

						offset += GetFieldSizeByName(fieldName, m_typeArray.at(field.typeIndex).length);
					}

					break;
				}
			}

			return offset;
		}

		std::optional<blender::FileBlock> FindParentObject(blender::PtrType oldAddressOfBlock) const
		{
			for(const auto& block: m_blockArray)
			{
				if(!Identify(block.desc.code, blender::BlockOB, 4))
					continue;

				const size_t offsetOfDataPtr = GetFieldOffset("Object", "*data");
				const blender::PtrType dataPtr = *PeekTypePtr<blender::PtrType>(block.data, offsetOfDataPtr);

				if(dataPtr == oldAddressOfBlock)
					return { block };
			}

			return {};
		}

		size_t GetStructSizeByName(const std::string_view structName) const
		{
			for(const auto& structDesc: m_structArray)
			{
				const TypeInfo& structTypeInfo = m_typeArray.at(structDesc.typeIndex);

				if(structName == structTypeInfo.type.AsString())
					return structTypeInfo.length;
			}

			return 0;
		}

		size_t GetFieldSizeByName(const std::string_view fieldName, const size_t fieldLen) const
		{
			const size_t length = (fieldName.starts_with('*') || fieldName.starts_with("(*") ? sizeof(blender::PtrType) : fieldLen);
			size_t count = 1;

			const size_t idxOfArray = fieldName.find_first_of('[');
			if(idxOfArray != std::string::npos)
			{
				MemorySpan filedNameSpan = { (uint8_t*)fieldName.data() + idxOfArray, (uint8_t*)fieldName.data() + fieldName.length() };
				while(filedNameSpan.Size() > 0)
				{
					if(*filedNameSpan.Data() == '[')
					{
						filedNameSpan.Advance();
						size_t num = 0;

						while(isdigit(*filedNameSpan.Data()))
						{
							const uint8_t digit = *filedNameSpan.Data();
							num = num * 10 + (digit - '0');
							filedNameSpan.Advance();
						}

						count *= num;
						filedNameSpan.Advance(); // ']'
					}
					else if(*filedNameSpan.Data() == '\0')
					{
						break;
					}
				}
			}

			return length * count;
		}

		bool IdentifyStruct(size_t id, const std::string_view name) const
		{
			assert(id < m_structArray.size());
			const StructDesc& structDesc = m_structArray.at(id);
			const TypeInfo& structTypeInfo = m_typeArray.at(structDesc.typeIndex);

			return (name == structTypeInfo.type.AsString());
		}

		bool Identify(const uint8_t* bytes, const char* id, uint32_t n) const
		{
			return memcmp(bytes, id, sizeof(uint8_t) * n) == 0;
		}

		/*DEBUG*/
		std::string_view GetBlockNameByID(const blender::FileBlock& block, bool offsetBy2)
		{
			const std::string_view nameView(PeekTypePtr<char>(block.data, GetFieldOffset("ID", "name[66]")));
			return (offsetBy2 ? nameView.substr(2) : nameView);
		}

		/*DEBUG*/
		std::string_view GetStructNameBySDNA(const size_t sdnaIndex)
		{
			assert(sdnaIndex < m_structArray.size());
			const auto typeIndex = m_structArray.at(sdnaIndex).typeIndex;
			return m_typeArray.at(typeIndex).type.AsString();
		}

		/*DEBUG*/
		void PrintBlockSDNA(const blender::FileBlock& block)
		{
			const blender::FileBlockDesc64& desc = block.desc;
			std::cout << "block code: '" << std::string_view(reinterpret_cast<const char*>(desc.code), 4) << 
						 "', sdna: " << desc.sdnaIndex << 
						 ", count: " << desc.count << 
						 ", size: " << block.data.Size() << 
						 ", offset: 0x" << std::hex << block.fileOffset << '\n';
		}

		/*DEBUG*/
		void PrintStructBySDNA(const size_t sdnaIndex, bool fields = true)
		{
			assert(sdnaIndex < m_structArray.size());
			PrintStruct(m_structArray.at(sdnaIndex), fields);
		}

		/*DEBUG*/
		void PrintStructByName(const std::string_view name, bool fields = true)
		{
			for(const auto& structDesc: m_structArray)
			{
				const TypeInfo& structTypeInfo = m_typeArray.at(structDesc.typeIndex);

				if(name == structTypeInfo.type.AsString())
				{
					PrintStruct(structDesc, fields);
					return;
				}
			}
		}

		/*DEBUG*/
		void PrintStruct(const StructDesc& structDesc, bool fields = true)
		{
			const TypeInfo& structTypeInfo = m_typeArray.at(structDesc.typeIndex);

			std::cout << "struct " << structTypeInfo.type.Data() << " (length: " << structTypeInfo.length << ")\n";
			size_t offset = 0;

			if(!fields)
				return;

			std::cout << "{\n";

			for(const auto& field: structDesc.fields)
			{
				const std::string_view fieldName = m_nameArray.at(field.nameIndex).AsString();
				std::cout << '\t' << m_typeArray.at(field.typeIndex).type.AsString() << ' ' << fieldName << ";\t\t// " << std::dec << offset << '\n';

				offset += GetFieldSizeByName(fieldName, m_typeArray.at(field.typeIndex).length);
			}
			std::cout << "};\n";
		}

		void Cleanup()
		{
			m_blockArray.clear();
			m_nameArray.clear();
			m_typeArray.clear();
			m_structArray.clear();

			if(!m_fileSpan.Empty())
				delete[] m_fileSpan.begin;
		}

		MemorySpan m_fileSpan;

		std::vector<blender::FileBlock> m_blockArray;

		struct TypeInfo
		{
			MemorySpan type;
			uint16_t length;
		};

		std::vector<MemorySpan> m_nameArray;
		std::vector<TypeInfo> m_typeArray;

		struct FieldDesc
		{
			uint16_t typeIndex;
			uint16_t nameIndex;
		};

		struct StructDesc
		{
			uint16_t typeIndex;
			std::vector<FieldDesc> fields;
		};

		std::vector<StructDesc> m_structArray;
};

int main()
{
	blendExpl blend;
	blend.Explore();

	std::cout << "\nPress enter key to quit...";
	std::cin.get();
}