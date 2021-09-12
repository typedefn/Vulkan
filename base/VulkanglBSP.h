#pragma once

#include <stdlib.h>
#include <string>
#include <fstream>
#include <vector>
#include <stdexcept>

#include "vulkan/vulkan.h"
#include "VulkanDevice.h"

#include <ktx.h>
#include <ktxvulkan.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#define TINYGLTF_NO_STB_IMAGE_WRITE
#ifdef VK_USE_PLATFORM_ANDROID_KHR
#define TINYGLTF_ANDROID_LOAD_FROM_ASSETS
#endif
#include "tiny_gltf.h"

#if defined(__ANDROID__)
#include <android/asset_manager.h>
#endif

#define DotProduct(x,y) (x[0]*y[0]+x[1]*y[1]+x[2]*y[2])
#define DotProduct2(x,y) (x[0]*y[0]+x[1]*y[1])
#define DoublePrecisionDotProduct(x,y) ((double)x[0]*y[0]+(double)x[1]*y[1]+(double)x[2]*y[2])
#define VectorSubtract(a,b,c) {c[0]=a[0]-b[0];c[1]=a[1]-b[1];c[2]=a[2]-b[2];}
#define VectorAdd(a,b,c) {c[0]=a[0]+b[0];c[1]=a[1]+b[1];c[2]=a[2]+b[2];}
#define VectorCopy(a,b) {b[0]=a[0];b[1]=a[1];b[2]=a[2];}
#define q_max(a, b) (((a) > (b)) ? (a) : (b))

#define MAX_QPATH 64    // max length of a quake game pathname
#define MAX_MAP_HULLS   4
#define NUM_AMBIENTS      4   // automatic ambient sounds
#define ES_SOLID_NOT 0
#define ES_SOLID_BSP 31
#define ES_SOLID_HULL1 0x80201810
#define ES_SOLID_HULL2 0x80401820
#define MAXLIGHTMAPS  4
#define MAX_DLIGHTS   64 //johnfitz -- was 32
#define VERTEXSIZE  7
#define IDPOLYHEADER  (('O'<<24)+('P'<<16)+('D'<<8)+'I')
#define HEADER_LUMPS  15
#define BSPVERSION  29

#define LUMP_ENTITIES 0
#define LUMP_PLANES   1
#define LUMP_TEXTURES 2
#define LUMP_VERTEXES 3
#define LUMP_VISIBILITY 4
#define LUMP_NODES    5
#define LUMP_TEXINFO  6
#define LUMP_FACES    7
#define LUMP_LIGHTING 8
#define LUMP_CLIPNODES  9
#define LUMP_LEAFS    10
#define LUMP_MARKSURFACES 11
#define LUMP_EDGES    12
#define LUMP_SURFEDGES  13
#define LUMP_MODELS   14

#define HEADER_LUMPS  15
/* RMQ support (2PSB). 32bits instead of shorts for all but bbox sizes (which
 * still use shorts) */
#define BSP2VERSION_2PSB (('B' << 24) | ('S' << 16) | ('P' << 8) | '2')

/* BSP2 support. 32bits instead of shorts for everything (bboxes use floats) */
#define BSP2VERSION_BSP2 (('B' << 0) | ('S' << 8) | ('P' << 16) | ('2'<<24))

#define LOADFILE_STACK    4
#define MAX_OSPATH 1024
#define MAX_HANDLES   32  /* johnfitz -- was 10 */
#define MAX_FILES_IN_PACK 2048

#define SURF_PLANEBACK    2
#define SURF_DRAWSKY    4
#define SURF_DRAWSPRITE   8
#define SURF_DRAWTURB   0x10
#define SURF_DRAWTILED    0x20
#define SURF_DRAWBACKGROUND 0x40
#define SURF_UNDERWATER   0x80
#define SURF_NOTEXTURE    0x100 //johnfitz
#define SURF_DRAWFENCE    0x200
#define SURF_DRAWLAVA   0x400
#define SURF_DRAWSLIME    0x800
#define SURF_DRAWTELE   0x1000
#define SURF_DRAWWATER    0x2000

#define TEXPREF_NONE      0x0000
#define TEXPREF_MIPMAP      0x0001  // generate mipmaps
// TEXPREF_NEAREST and TEXPREF_LINEAR aren't supposed to be ORed with TEX_MIPMAP
#define TEXPREF_LINEAR      0x0002  // force linear
#define TEXPREF_NEAREST     0x0004  // force nearest
#define TEXPREF_ALPHA     0x0008  // allow alpha
#define TEXPREF_PAD     0x0010  // allow padding
#define TEXPREF_PERSIST     0x0020  // never free
#define TEXPREF_OVERWRITE   0x0040  // overwrite existing same-name texture
#define TEXPREF_NOPICMIP    0x0080  // always load full-sized
#define TEXPREF_FULLBRIGHT    0x0100  // use fullbright mask palette
#define TEXPREF_NOBRIGHT    0x0200  // use nobright mask palette
#define TEXPREF_CONCHARS    0x0400  // use conchars palette
#define TEXPREF_WARPIMAGE   0x0800  // resize this texture when warpimagesize changes


#define TEX_SPECIAL   1   // sky or slime, no lightmap or 256 subdivision
#define TEX_MISSING   2   // johnfitz -- this texinfo does not have a texture

typedef unsigned char byte;
typedef uintptr_t src_offset_t;
typedef float soa_aabb_t[2 * 3 * 8]; // 8 AABB's in SoA form
typedef float soa_plane_t[4 * 8]; // 8 planes in SoA form

namespace vkglBSP {
enum DescriptorBindingFlags {
  ImageBaseColor = 0x00000001, ImageNormalMap = 0x00000002
};

enum ModType {
  ModBrush, ModSprite, ModAlias
};

enum SyncType {
  Sync = 0, Rand, FrameTime/*sync to when .frame changes*/
};

enum SrcFormat {
  Indexed, LightMap, RGBA
};

extern VkDescriptorSetLayout descriptorSetLayoutImage;
extern VkDescriptorSetLayout descriptorSetLayoutUbo;
extern VkMemoryPropertyFlags memoryPropertyFlags;
extern uint32_t descriptorBindingFlags;

struct Node;
struct QTexture;
struct MTexInfo;

struct TexInfo
{
  float   vecs[2][4];   // [s/t][xyz offset]
  int     miptex;
  int     flags;
};

// note that edge 0 is never used, because negative edge nums are used for
// counterclockwise use of the edge in a face
struct DSEdge {
  unsigned short v[2];   // vertex numbers
};

struct DLEdge {
  unsigned int v[2];   // vertex numbers
};

struct DSFace {
  short planenum;
  short side;

  int firstedge;    // we must support > 64k edges
  short numedges;
  short texinfo;

// lighting info
  byte styles[MAXLIGHTMAPS];
  int lightofs;   // start of [numstyles*surfsize] samples
};

struct DLFace {
  int planenum;
  int side;

  int firstedge;    // we must support > 64k edges
  int numedges;
  int texinfo;

// lighting info
  byte styles[MAXLIGHTMAPS];
  int lightofs;   // start of [numstyles*surfsize] samples
};

struct DMipTexLump {
  int nummiptex;
  int dataofs[4];   // [nummiptex]
};

struct DVertex {
  float point[3];
};

struct MVertex {
  glm::vec4 position;
};

struct MEdge {
  unsigned int v[2];
  unsigned int cachededgeoffset;
};

struct CacheUser {
  void *data;
};

struct Lump {
  int fileofs, filelen;
};

struct EntityState {
  glm::vec3 origin;
  glm::vec3 angles;
  unsigned short modelindex; //johnfitz -- was int
  unsigned short frame;    //johnfitz -- was int
  unsigned int effects;
  unsigned char colormap; //johnfitz -- was int
  unsigned char skin;   //johnfitz -- was int
  unsigned char scale;    //spike -- *16
  unsigned char pmovetype;  //spike
  unsigned short traileffectnum; //spike -- for qc-defined particle trails. typically evilly used for things that are not trails.
  unsigned short emiteffectnum; //spike -- for qc-defined particle trails. typically evilly used for things that are not trails.
  short velocity[3];  //spike -- the player's velocity.
  unsigned char eflags;
  unsigned char tagindex;
  unsigned short tagentity;
  unsigned short pad;
  unsigned char colormod[3];  //spike -- entity tints, *32
  unsigned char alpha;    //johnfitz -- added
  unsigned int solidsize;  //for csqc prediction logic.

};

struct QModel;
struct Texture;

struct GLTexture {
//managed by texture manager
  GLTexture *next;
  QModel *owner;
//managed by image loading
  char name[64];
  unsigned int width; //size of image as it exists in opengl
  unsigned int height; //size of image as it exists in opengl
  unsigned int flags;
  char source_file[MAX_QPATH]; //relative filepath to data source, or "" if source is in memory
  src_offset_t source_offset; //byte offset into file, or memory address
  SrcFormat source_format; //format of pixel data (indexed, lightmap, or rgba)
  unsigned int source_width; //size of image in source data
  unsigned int source_height; //size of image in source data
  unsigned short source_crc; //generated by source data before modifications
  signed char shirt; //0-13 shirt color, or -1 if never colormapped
  signed char pants; //0-13 pants color, or -1 if never colormapped
//used for rendering
  VkImage image;
  VkImageView target_image_view;
  struct glheap_s *heap;
  struct glheapnode_s *heap_node;
  VkDescriptorSet descriptor_set;
  VkFramebuffer frame_buffer;
  VkDescriptorSet warp_write_descriptor_set;
  VkDeviceMemory deviceMemory;
  VkSampler sampler;
  VkImageView view;
  VkImageLayout imageLayout;
  uint32_t mipLevels;
  int visframe; //matches r_framecount if texture was bound this frame
};

// plane_t structure
struct MPlane {
  glm::vec3 normal;
  float dist;
  byte type;     // for texture axis selection and fast side tests
  byte signbits;   // signx + signy<<1 + signz<<1
  byte pad[2];
};

//johnfitz -- for clipnodes>32k
struct MClipNode {
  int planenum;
  int children[2]; // negative numbers are contents
};

struct EFrag {
  struct EFrag *leafnext;
  struct entity_s *entity;
};

#define MIPLEVELS 4

struct MipTex {
  char name[16];
  unsigned width, height;
  unsigned offsets[MIPLEVELS];   // four mip maps stored
};

struct Hull {
  MClipNode *clipnodes; //johnfitz -- was dclipnode_t
  MPlane *planes;
  int firstclipnode;
  int lastclipnode;
  glm::vec3 clip_mins;
  glm::vec3 clip_maxs;
};

struct GlPoly {
  struct GlPoly *next;
  int numverts;
//  float verts[4][VERTEXSIZE]; // variable sized (xyz s1t1 s2t2)
  std::vector<glm::vec4> verts;
};


struct MSurface {
  int visframe;   // should be drawn when node is crossed

  MPlane *plane;
  int flags;

  int firstedge;  // look up in model->surfedges[], negative numbers
  int numedges; // are backwards edges

  short texturemins[2];
  short extents[2];

  int light_s, light_t; // gl lightmap coordinates

  GlPoly polys;       // multiple if warped
  MSurface *texturechain;

  MTexInfo *texinfo;

  int vbo_firstvert;    // index of this surface's first vert in the VBO

// lighting info
  int dlightframe;
  unsigned int dlightbits[(MAX_DLIGHTS + 31) >> 5];
  // int is 32 bits, need an array for MAX_DLIGHTS > 32

  int lightmaptexturenum;
  byte styles[MAXLIGHTMAPS];
  int cached_light[MAXLIGHTMAPS]; // values currently used in lightmap
  bool cached_dlight;        // true if dynamic light in cache
  byte *samples;   // [numstyles*surfsize]
};


struct MNode {
// common with leaf
  int contents;   // 0, to differentiate from leafs
  int visframe;   // node needs to be traversed if current

  float minmaxs[6];   // for bounding box culling

  struct MNode *parent;

// node specific
  MPlane *plane;
  struct MNode *children[2];

  unsigned int firstsurface;
  unsigned int numsurfaces;
};


struct QTexture {
  char name[16];
  unsigned      width, height;
  GLTexture *gltexture; //johnfitz -- pointer to gltexture
  GLTexture *fullbright; //johnfitz -- fullbright mask texture
  GLTexture *warpimage; //johnfitz -- for water animation
  bool update_warp; //johnfitz -- update warp this frame
  MSurface *texturechains[2];  // for texture chains
  int anim_total;       // total tenths in sequence ( 0 = no)
  int anim_min, anim_max;   // time for this frame min <=time< max
  QTexture *anim_next;   // in the animation sequence
  QTexture *alternate_anims; // bmodels in frmae 1 use these
  unsigned offsets[MIPLEVELS];   // four mip maps stored
};


struct MTexInfo {
  float vecs[2][4];
  QTexture texture;
  int flags;
};



struct MLeaf {
// common with node
  int contents;   // wil be a negative contents number
  int visframe;   // node needs to be traversed if current

  float minmaxs[6];   // for bounding box culling

  struct MNode *parent;

// leaf specific
  byte *compressed_vis;
  EFrag *efrags;

  int *firstmarksurface;
  int nummarksurfaces;
  int key;      // BSP sequence number for leaf's contents
  byte ambient_sound_level[NUM_AMBIENTS];
};

struct DHeader {
  int version;
  Lump lumps[HEADER_LUMPS];
};

struct DModel {
  float mins[3], maxs[3];
  float origin[3];
  int headnode[MAX_MAP_HULLS];
  int visleafs;   // not including the solid leaf 0
  int firstface, numfaces;
};

struct QModel {
  char name[MAX_QPATH];
  unsigned int path_id;		// path id of the game directory
  // that this model came from
  bool needload;		// bmodels and sprites don't cache normally

  ModType type;
  int numframes;
  SyncType syncType;

  int flags;

  //
  // volume occupied by the model graphics
  //
  glm::vec3 mins, maxs;
  glm::vec3 ymins, ymaxs; //johnfitz -- bounds for entities with nonzero yaw
  glm::vec3 rmins, rmaxs; //johnfitz -- bounds for entities with nonzero pitch or roll
  //johnfitz -- removed float radius;

  //
  // solid volume for clipping
  //
  bool clipbox;
  glm::vec3 clipmins, clipmaxs;

  //
  // brush model
  //
  int firstmodelsurface, nummodelsurfaces;

  int numsubmodels;
  DModel *submodels;

  int numplanes;
  MPlane *planes;

  int numleafs;		// number of visible leafs, not counting 0
  MLeaf *leafs;

  int numvertexes;
  std::vector<MVertex> vertexes;

  int numedges;
  std::vector<uint32_t> edges;
  std::vector<MEdge> medges;

  int numnodes;
  MNode *nodes;

  int numtexinfo;
  std::vector<MTexInfo> texinfo;

  int numsurfaces;
  std::vector<MSurface> surfaces;

  int numsurfedges;
  std::vector<int> surfedges;

  int numclipnodes;
  MClipNode *clipnodes; //johnfitz -- was dclipnode_t

  int nummarksurfaces;
  int *marksurfaces;

  soa_aabb_t *soa_leafbounds;
  byte *surfvis;
  soa_plane_t *soa_surfplanes;

  Hull hulls[MAX_MAP_HULLS];

  int numtextures;
  std::vector<QTexture> textures;

  byte *visdata;
  byte *lightdata;
  char *entities;

  bool viswarn; // for Mod_DecompressVis()

  int bspversion;
  int contentstransparent; //spike -- added this so we can disable glitchy wateralpha where its not supported.

  //
  // alias model
  //
  vks::Buffer vertexBuffer;
  vks::Buffer indexBuffer;

  struct glheap_s *vertex_heap;
  struct glheapnode_s *vertex_heap_node;

  struct glheap_s *index_heap;
  struct glheapnode_s *index_heap_node;
  int vboindexofs;    // offset in vbo of the hdr->numindexes unsigned shorts
  int vboxyzofs; // offset in vbo of hdr->numposes*hdr->numverts_vbo meshxyz_t
  int vbostofs;       // offset in vbo of hdr->numverts_vbo meshst_t

  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkDeviceMemory index_memory = VK_NULL_HANDLE;
  //
  // additional model data
  //
  CacheUser cache;		// only access through Mod_Extradata

};

struct Entity {
  bool forcelink;    // model changed

  int update_type;

  EntityState baseline;   // to fill in defaults in updates
  EntityState netstate;   // the latest network state

  double msgtime;    // time of last update
  glm::vec3 msg_origins[2]; // last two updates (0 is newest)
  glm::vec3 origin;
  glm::vec3 msg_angles[2];  // last two updates (0 is newest)
  glm::vec3 angles;
  struct QModel *model;     // NULL = no model
  struct EFrag *efrag;     // linked list of efrags
  int frame;
  float syncbase;   // for client-side animations
  byte *colormap;
  int effects;    // light, particles, etc
  int skinnum;    // for Alias models
  int visframe;   // last frame this entity was
  //  found in an active leaf

  int dlightframe;  // dynamic lighting
  int dlightbits;

// FIXME: could turn these into a union
  int trivial_accept;
  struct MNode *topnode;   // for bmodels, first world node
  //  that splits bmodel, or NULL if
  //  not split

  byte eflags; //spike -- mostly a mirror of netstate, but handles tag inheritance (eww!)
  byte alpha;      //johnfitz -- alpha
  byte lerpflags;    //johnfitz -- lerping
  float lerpstart;    //johnfitz -- animation lerping
  float lerptime;   //johnfitz -- animation lerping
  float lerpfinish; //johnfitz -- lerping -- server sent us a more accurate interval, use it instead of 0.1
  short previouspose; //johnfitz -- animation lerping
  short currentpose;  //johnfitz -- animation lerping
//  short         futurepose;   //johnfitz -- animation lerping
  float movelerpstart;  //johnfitz -- transform lerping
  glm::vec3 previousorigin; //johnfitz -- transform lerping
  glm::vec3 currentorigin;  //johnfitz -- transform lerping
  glm::vec3 previousangles; //johnfitz -- transform lerping
  glm::vec3 currentangles;  //johnfitz -- transform lerping
};

// QUAKEFS
struct PackFile {
  char name[MAX_QPATH];
  int filepos, filelen;
};

struct Pack {
  char filename[MAX_OSPATH];
  FILE *handle;
  int numfiles;
  std::vector<PackFile> files;
};

//
// on-disk pakfile
//
struct DPackFile {
  char name[56];
  int filepos, filelen;
};

struct DPackHeader {
  char id[4];
  int dirofs;
  int dirlen;
};

/*
 glTF texture loading class
 // */
struct Texture {
  vks::VulkanDevice *device = nullptr;
  VkImage image;
  VkImageLayout imageLayout;
  VkDeviceMemory deviceMemory;
  VkImageView view;
  uint32_t width, height;
  uint32_t mipLevels;
  uint32_t layerCount;
  VkDescriptorImageInfo descriptor;
  VkSampler sampler;
  void updateDescriptor();
  void destroy();
  void fromglTfImage(tinygltf::Image &gltfimage, std::string path,
      vks::VulkanDevice *device, VkQueue copyQueue);
};


/*
 glTF material class
 */
struct Material {
  vks::VulkanDevice *device = nullptr;
  enum AlphaMode {
    ALPHAMODE_OPAQUE, ALPHAMODE_MASK, ALPHAMODE_BLEND
  };
  AlphaMode alphaMode = ALPHAMODE_OPAQUE;
  float alphaCutoff = 1.0f;
  float metallicFactor = 1.0f;
  float roughnessFactor = 1.0f;
  glm::vec4 baseColorFactor = glm::vec4(1.0f);
  vkglBSP::Texture *baseColorTexture = nullptr;
  vkglBSP::Texture *metallicRoughnessTexture = nullptr;
  vkglBSP::Texture *normalTexture = nullptr;
  vkglBSP::Texture *occlusionTexture = nullptr;
  vkglBSP::Texture *emissiveTexture = nullptr;

  vkglBSP::Texture *specularGlossinessTexture;
  vkglBSP::Texture *diffuseTexture;

  VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

  Material(vks::VulkanDevice *device) :
      device(device) {
  }
  ;
  void createDescriptorSet(VkDescriptorPool descriptorPool,
      VkDescriptorSetLayout descriptorSetLayout,
      uint32_t descriptorBindingFlags);
};

/*
 glTF primitive
 */
struct Primitive {
  uint32_t firstIndex;
  uint32_t indexCount;
  uint32_t firstVertex;
  uint32_t vertexCount;
  Material &material;

  struct Dimensions {
    glm::vec3 min = glm::vec3(FLT_MAX);
    glm::vec3 max = glm::vec3(-FLT_MAX);
    glm::vec3 size;
    glm::vec3 center;
    float radius;
  } dimensions;

  void setDimensions(glm::vec3 min, glm::vec3 max);
  Primitive(uint32_t firstIndex, uint32_t indexCount, Material &material) :
      firstIndex(firstIndex), indexCount(indexCount), material(material) {
  }
  ;
};

/*
 glTF mesh
 */
struct Mesh {
  vks::VulkanDevice *device;

  std::vector<Primitive*> primitives;
  std::string name;

  struct UniformBuffer {
    VkBuffer buffer;
    VkDeviceMemory memory;
    VkDescriptorBufferInfo descriptor;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    void *mapped;
  } uniformBuffer;

  struct UniformBlock {
    glm::mat4 matrix;
    glm::mat4 jointMatrix[64] { };
    float jointcount { 0 };
  } uniformBlock;

  Mesh(vks::VulkanDevice *device, glm::mat4 matrix);
  ~Mesh();
};

/*
 glTF skin
 */
struct Skin {
  std::string name;
  Node *skeletonRoot = nullptr;
  std::vector<glm::mat4> inverseBindMatrices;
  std::vector<Node*> joints;
};

/*
 glTF node
 */
struct Node {
  Node *parent;
  uint32_t index;
  std::vector<Node*> children;
  glm::mat4 matrix;
  std::string name;
  Mesh *mesh;
  Skin *skin;
  int32_t skinIndex = -1;
  glm::vec3 translation { };
  glm::vec3 scale { 1.0f };
  glm::quat rotation { };
  glm::mat4 localMatrix();
  glm::mat4 getMatrix();
  void update();
  ~Node();
};

/*
 glTF animation channel
 */
struct AnimationChannel {
  enum PathType {
    TRANSLATION, ROTATION, SCALE
  };
  PathType path;
  Node *node;
  uint32_t samplerIndex;
};

/*
 glTF animation sampler
 */
struct AnimationSampler {
  enum InterpolationType {
    LINEAR, STEP, CUBICSPLINE
  };
  InterpolationType interpolation;
  std::vector<float> inputs;
  std::vector<glm::vec4> outputsVec4;
};

/*
 glTF animation
 */
struct Animation {
  std::string name;
  std::vector<AnimationSampler> samplers;
  std::vector<AnimationChannel> channels;
  float start = std::numeric_limits<float>::max();
  float end = std::numeric_limits<float>::min();
};

/*
 glTF default vertex layout with easy Vulkan mapping functions
 */
enum class VertexComponent {
  Position, Normal, UV, Color, Tangent, Joint0, Weight0
};

struct Vertex {
  glm::vec3 pos;
  glm::vec3 normal;
  glm::vec2 uv;
  glm::vec4 color;
  glm::vec4 joint0;
  glm::vec4 weight0;
  glm::vec4 tangent;
  static VkVertexInputBindingDescription vertexInputBindingDescription;
  static std::vector<VkVertexInputAttributeDescription> vertexInputAttributeDescriptions;
  static VkPipelineVertexInputStateCreateInfo pipelineVertexInputStateCreateInfo;
  static VkVertexInputBindingDescription inputBindingDescription(
      uint32_t binding);
  static VkVertexInputAttributeDescription inputAttributeDescription(
      uint32_t binding, uint32_t location, VertexComponent component);
  static std::vector<VkVertexInputAttributeDescription> inputAttributeDescriptions(
      uint32_t binding, const std::vector<VertexComponent> components);
  /** @brief Returns the default pipeline vertex input state create info structure for the requested vertex components */
  static VkPipelineVertexInputStateCreateInfo* getPipelineVertexInputState(
      const std::vector<VertexComponent> components);
};

enum FileLoadingFlags {
  None = 0x00000000,
  PreTransformVertices = 0x00000001,
  PreMultiplyVertexColors = 0x00000002,
  FlipY = 0x00000004,
  DontLoadImages = 0x00000008
};

enum RenderFlags {
  BindImages = 0x00000001,
  RenderOpaqueNodes = 0x00000002,
  RenderAlphaMaskedNodes = 0x00000004,
  RenderAlphaBlendedNodes = 0x00000008
};

/*
 glTF model loading and rendering class
 */
class Model {
private:
  vkglBSP::Texture* getTexture(uint32_t index);
  vkglBSP::Texture emptyTexture;
  void createEmptyTexture(VkQueue transferQueue);
  byte *loadbuf;
  int loadsize;

  char loadname[32]; // for hunk tags
  byte *mod_base;
  char errorBuff[256];
  Pack *pak0;
  std::vector<MVertex> backupVertex;
  std::vector<uint32_t> backupIndex;
  QModel mod;
  VkDescriptorSet descriptorSet;
  MSurface *warpface;
std::vector<GlPoly> polygons;

public:
  QModel *loadmodel;
  vks::VulkanDevice *device;
  VkDescriptorPool descriptorPool;

  std::vector<Node*> nodes;
  std::vector<Node*> linearNodes;

  std::vector<Skin*> skins;

  std::vector<Texture> textures;
  std::vector<Material> materials;
  std::vector<Animation> animations;

  struct Dimensions {
    glm::vec3 min = glm::vec3(FLT_MAX);
    glm::vec3 max = glm::vec3(-FLT_MAX);
    glm::vec3 size;
    glm::vec3 center;
    float radius;
  } dimensions;

  bool metallicRoughnessWorkflow = true;
  bool buffersBound = false;
  std::string path;

  Model() {
  }
  ;
  ~Model();
  void loadNode(vkglBSP::Node *parent, const tinygltf::Node &node,
      uint32_t nodeIndex, const tinygltf::Model &model,
      std::vector<uint32_t> &indexBuffer, std::vector<Vertex> &vertexBuffer,
      float globalscale);
  void loadSkins(tinygltf::Model &gltfModel);
  void loadImages(tinygltf::Model &gltfModel, vks::VulkanDevice *device,
      VkQueue transferQueue);
  void loadMaterials(tinygltf::Model &gltfModel);
  void loadAnimations(tinygltf::Model &gltfModel);
  void loadFromFile(std::string filename, vks::VulkanDevice *device,
      VkQueue transferQueue, uint32_t fileLoadingFlags =
          vkglBSP::FileLoadingFlags::None, float scale = 1.0f);
  void bindBuffers(VkCommandBuffer commandBuffer);
  void draw(const VkCommandBuffer commandBuffer, VkPipeline pipeline,
      VkPipelineLayout pipelineLayout);
  void getNodeDimensions(Node *node, glm::vec3 &min, glm::vec3 &max);
  void getSceneDimensions();
  void updateAnimation(uint32_t index, float time);
  Node* findNode(Node *parent, uint32_t index);
  Node* nodeFromIndex(uint32_t index);
  void prepareNodeDescriptor(vkglBSP::Node *node,
      VkDescriptorSetLayout descriptorSetLayout);

  /*
   ==================
   Mod_ForName

   Loads in a model for the given name
   ==================
   */
  QModel* modForName(const char *name, bool crash);
  QModel* modLoadModel(vkglBSP::QModel *mod, bool crash);

  // uses temp hunk if larger than bufsize
  byte* comLoadStackFile(const char *path, void *buffer, int bufsize,
      unsigned int *path_id);

  /*
   ============
   COM_FileBase
   take 'somedir/otherdir/filename.ext',
   write only 'filename' to the output
   ============
   */
  void comFileBase(const char *in, char *out, size_t outsize);

  size_t q_strlcpy(char *dst, const char *src, size_t siz);
  void modLoadBrushModel(QModel *mod, void *buffer);

  /*
   =================
   Mod_LoadVertexes
   =================
   */
  void modLoadVertexes(Lump *l);

  void init();
  Pack comLoadPackFile(const char *packfile);
  int sysFileOpenRead(const char *path, int *hndl);
  long sysFileLength(FILE *f);
  PackFile comFindFile(const char *filename);
  void modLoadEdges(Lump *l);
  void modLoadSurfedges(Lump *l);
  void modLoadFaces(Lump *l);
  void modPolyForUnlitSurface(MSurface *fa);
  void modLoadTextures (Lump *l);
  void modLoadTexInfo(Lump *l);
  void boundPoly (int numverts, std::vector<glm::vec3> &verts, glm::vec3 & mins, glm::vec3 & maxs);
  void subdividePolygon (int numverts, std::vector<glm::vec3> & verts);
  void glSubdivideSurface (MSurface *fa);

  static inline int q_tolower(int c) {
    return ((q_isupper(c)) ? (c | ('a' - 'A')) : c);
  }

  static inline int q_isupper(int c) {
    return (c >= 'A' && c <= 'Z');
  }

  int q_strncasecmp(const char *s1, const char *s2, size_t n) {
    const char *p1 = s1;
    const char *p2 = s2;
    char c1, c2;

    if (p1 == p2 || n == 0)
      return 0;

    do {
      c1 = q_tolower(*p1++);
      c2 = q_tolower(*p2++);
      if (c1 == '\0' || c1 != c2)
        break;
    } while (--n > 0);

    return (int) (c1 - c2);
  }

  void comStripExtension(const char *in, char *out, size_t outsize) {
    int length;

    if (!*in) {
      *out = '\0';
      return;
    }
    if (in != out) /* copy when not in-place editing */
      q_strlcpy(out, in, outsize);
    length = (int) strlen(out) - 1;
    while (length > 0 && out[length] != '.') {
      --length;
      if (out[length] == '/' || out[length] == '\\')
        return; /* no extension */
    }
    if (length > 0)
      out[length] = '\0';
  }

};

}
