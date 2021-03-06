#define C_IO 0 // settings for m_io_method
#define WIN_IO 1

// P2G transfer algorithms
#define SCATTER_REDUCE 0
#define SCATTER 1
#define GATHER 2

// GVDB library
#include "gvdb.h"
using namespace nvdb;

// Sample utils
#include "cudaProfiler.h"
#include "main.h"   // window system
#include "nv_gui.h" // gui system
#include <GL/glew.h>
#include "cuda_runtime_api.h"
#include <fstream>

#include "string_helper.h"

VolumeGVDB gvdb;

#ifdef USE_OPTIX
// OptiX scene
#include "optix_scene.h"
OptixScene optx;
#endif

struct PolyModel {
    char fpath[1024];
    char fname[1024];
    int mat;
    float scal;
    Vector3DF offs;
};

class Sample : public NVPWindow {
  public:
    Sample();
    virtual bool init();
    virtual void display();
    virtual void reshape(int w, int h);
    virtual void motion(int x, int y, int dx, int dy);
    virtual void keyboardchar(unsigned char key, int mods, int x, int y);
    virtual void mouse(NVPWindow::MouseButton button, NVPWindow::ButtonAction state, int mods,
                       int x, int y);
    virtual void on_arg(std::string arg, std::string val);

    void parse_scene(std::string fname);
    void parse_value(int mode, std::string tag, std::string val);
    void add_material(bool bDeep);
    void add_model();
    void load_points(std::string pntpath, std::string pntfile, int frame);
    void load_polys(std::string polypath, std::string polyfile, int frame, float pscale,
                    Vector3DF poffs, int pmat);
    void clear_gvdb();
    void render_update();
    void render_frame();
    void draw_points();
    void draw_topology(); // draw gvdb topology
    void start_guis(int w, int h);
    void ClearOptix();
    void RebuildOptixGraph(int shading);
    void ReportMemory();

    int m_radius;
    Vector3DF m_origin;
    float m_renderscale;

    DataPtr m_particlePositions;
    DataPtr m_particleMasses;
    DataPtr m_particleVelocities;
    DataPtr m_particleDeformationGradients;
    DataPtr m_particleAffineStates;
    float m_particleInitialVolume;

    float simulationFPS;
    float deltaTime;
    float elapsedTime;
    int elapsedTimeSteps;

    int m_w, m_h;
    int m_numpnts;
    DataPtr m_pnt1;
    int m_frame;
    int m_fstep;
    int m_sample;
    int m_max_samples;
    int m_shade_style;
    int gl_screen_tex;
    int mouse_down;
    bool m_render_optix;
    bool m_show_points;
    bool m_show_topo;
    bool m_save_png;

    int m_smooth;
    Vector3DF m_smoothp;

    bool m_pnton; // point time series
    std::string m_pntpath;
    std::string m_pntfile;
    int m_pntmat;

    bool m_polyon; // polygon time series
    std::string m_polypath;
    std::string m_polyfile;
    int m_polymat;
    int m_pframe, m_pfstep;
    float m_pscale;
    Vector3DF m_poffset;

    std::string m_infile;
    std::string m_envfile;
    std::string m_outpath;
    std::string m_outfile;

    std::vector<MaterialParams> mat_list;
    std::vector<PolyModel> model_list;

    int m_iteration;
    int m_p2g_algorithm;
    bool m_p2g_only;
    int m_iteration_limit;
    int m_frame_limit;

    bool m_info;
    int m_io_method;
};

Sample sample_obj;

void handle_gui(int gui, float val) {
    switch (gui) {
        case 3: {                                    // Shading gui changed
            float alpha = (val == 4) ? 0.03f : 0.8f; // when in volume mode (#4),
                                                     // make volume very transparent
            gvdb.getScene()->LinearTransferFunc(0.00f, 0.50f, Vector4DF(0, 0, 1, 0),
                                                Vector4DF(0.0f, 1, 0, 0.1f));
            gvdb.getScene()->LinearTransferFunc(0.50f, 1.0f, Vector4DF(0.0f, 1, 0, 0.1f),
                                                Vector4DF(1.0f, .0f, 0, 0.1f));
            gvdb.CommitTransferFunc();
        } break;
    }
}

void Sample::start_guis(int w, int h) {
    clearGuis();
    setview2D(w, h);
    guiSetCallback(handle_gui);
    addGui(10, h - 30, 130, 20, "Points", GUI_CHECK, GUI_BOOL, &m_show_points, 0, 1.0f);
    addGui(150, h - 30, 130, 20, "Topology", GUI_CHECK, GUI_BOOL, &m_show_topo, 0, 1.0f);
}

void Sample::add_material(bool bDeep) {
    MaterialParams p;
    mat_list.push_back(p);
}
void Sample::add_model() {
    PolyModel p;
    strcpy(p.fpath, "");
    strcpy(p.fname, "");
    p.offs = Vector3DF(0, 0, 0);
    p.scal = 1;
    model_list.push_back(p);
}

void Sample::ClearOptix() { optx.ClearGraph(); }

void Sample::RebuildOptixGraph(int shading) {
    char filepath[1024];

    optx.ClearGraph();

    for (int n = 0; n < mat_list.size(); n++) {
        MaterialParams *p = &mat_list[n];
        int id = optx.AddMaterial("optix_trace_surface", "trace_surface", "trace_shadow");
        optx.SetMaterialParams(id, p);
    }

    optx.CreateEnvmap("");
    if (!m_envfile.empty()) {
        char fname[1024];
        strcpy(fname, m_envfile.c_str());
        if (gvdb.FindFile(fname, filepath)) {
            nvprintf("Loading env map %s.\n", filepath);
            optx.CreateEnvmap(filepath);
        }
    }

    if (mat_list.size() == 0) {
        nvprintf("Error: No materials have been specified in scene.\n");
        nverror();
    }

    /// Add deep volume material
    // mat_surf[1] = optx.AddMaterial("optix_trace_deep", "trace_deep",
    // "trace_shadow");

    // Add GVDB volume to the OptiX scene
    nvprintf("Adding GVDB Volume to OptiX graph.\n");
    char isect;
    switch (shading) {
        case SHADE_TRILINEAR:
            isect = 'S';
            break;
        case SHADE_VOLUME:
            isect = 'D';
            break;
        case SHADE_LEVELSET:
            isect = 'L';
            break;
        case SHADE_EMPTYSKIP:
            isect = 'E';
            break;
    }
    Vector3DF volmin = gvdb.getVolMin();
    Vector3DF volmax = gvdb.getVolMax();
    Matrix4F xform;
    xform.Identity();
    int atlas_glid = gvdb.getAtlasGLID(0);
    optx.AddVolume(atlas_glid, volmin, volmax, xform, mat_list[m_pntmat].id, isect);

    Model *m;

    // Add poly time series (optional)
    if (m_polyon) {
        m = gvdb.getScene()->getModel(0);
        nvprintf("Adding Polygon time series data.\n");
        xform.SRT(Vector3DF(1, 0, 0), Vector3DF(0, 1, 0), Vector3DF(0, 0, 1), Vector3DF(0, 0, 0),
                  m_renderscale);
        optx.AddPolygons(m, m_polymat, xform);
    }

    // Add polygonal models
    int id;
    for (int n = 0; n < model_list.size(); n++) {
        if (strlen(model_list[n].fpath) == 0) {
            gvdb.FindFile(model_list[n].fname, filepath);
        } else {
            sprintf(filepath, "%s%s", model_list[n].fpath, model_list[n].fname);
        }
        nvprintf("Load model %s...", filepath);
        id = gvdb.getScene()->AddModel(filepath, model_list[n].scal, model_list[n].offs.x,
                                       model_list[n].offs.y, model_list[n].offs.z);
        gvdb.CommitGeometry(id);

        m = gvdb.getScene()->getModel(id);
        xform.Identity();
        xform.SRT(Vector3DF(1, 0, 0), Vector3DF(0, 1, 0), Vector3DF(0, 0, 1), Vector3DF(0, 0, 0),
                  m_renderscale);
        optx.AddPolygons(m, model_list[n].mat, xform);
        nvprintf(" Done.\n");
    }

    // Set Transfer Function (once before validate)
    Vector4DF *src = gvdb.getScene()->getTransferFunc();
    optx.SetTransferFunc(src);

    // Validate OptiX graph
    nvprintf("Validating OptiX.\n");
    optx.ValidateGraph();

    // Assign GVDB data to OptiX
    nvprintf("Update GVDB Volume.\n");
    optx.UpdateVolume(&gvdb);

    nvprintf("Rebuild Optix.. Done.\n");
}

#define M_GLOBAL 0
#define M_RENDER 1
#define M_LIGHT 2
#define M_CAMERA 3
#define M_MODEL 4
#define M_POINTS 5
#define M_POLYS 6
#define M_VOLUME 7
#define M_MATERIAL 8

Sample::Sample() {
    m_frame = -1;
    m_renderscale = 0.0;
    m_infile = "small.scn";
    m_io_method = C_IO;

    m_iteration = 0;
    m_p2g_algorithm = SCATTER_REDUCE;
    m_p2g_only = false; // Do full MPM instead of only benchmark P2G levelset
    m_iteration_limit = 0; // Unlimited
    m_frame_limit = 0; // Unlimited
}

void Sample::parse_value(int mode, std::string tag, std::string val) {
    MaterialParams *matp;
    Vector3DF vec;

    switch (mode) {
        case M_POINTS:
            if (strEq(tag, "path"))
                m_pntpath = strTrim(val);
            if (strEq(tag, "file"))
                m_pntfile = strTrim(val);
            if (strEq(tag, "mat"))
                m_pntmat = strToNum(val);
            if (strEq(tag, "frame"))
                m_frame = strToNum(val);
            if (strEq(tag, "fstep"))
                m_fstep = strToNum(val);
            break;
        case M_POLYS:
            if (strEq(tag, "path"))
                m_polypath = val;
            if (strEq(tag, "file"))
                m_polyfile = val;
            if (strEq(tag, "mat"))
                m_polymat = strToNum(val);
            if (strEq(tag, "frame"))
                m_pframe = strToNum(val);
            if (strEq(tag, "fstep"))
                m_pfstep = strToNum(val);
            break;
        case M_MATERIAL: {
            int i = mat_list.size() - 1;
            matp = &mat_list[i];
            if (strEq(tag, "lightwid"))
                matp->light_width = strToNum(val);
            if (strEq(tag, "shwid"))
                matp->shadow_width = strToNum(val);
            if (strEq(tag, "shbias"))
                matp->shadow_bias = strToNum(val);
            if (strEq(tag, "ambient"))
                strToVec3(val, "<", ",", ">", &matp->amb_color.x);
            if (strEq(tag, "diffuse"))
                strToVec3(val, "<", ",", ">", &matp->diff_color.x);
            if (strEq(tag, "spec"))
                strToVec3(val, "<", ",", ">", &matp->spec_color.x);
            if (strEq(tag, "spow"))
                matp->spec_power = strToNum(val);
            if (strEq(tag, "env"))
                strToVec3(val, "<", ",", ">", &matp->env_color.x);
            if (strEq(tag, "reflwid"))
                matp->refl_width = strToNum(val);
            if (strEq(tag, "reflbias"))
                matp->refl_bias = strToNum(val);
            if (strEq(tag, "reflcolor"))
                strToVec3(val, "<", ",", ">", &matp->refl_color.x);
            if (strEq(tag, "refrwid"))
                matp->refr_width = strToNum(val);
            if (strEq(tag, "refrbias"))
                matp->refr_bias = strToNum(val);
            if (strEq(tag, "refrcolor"))
                strToVec3(val, "<", ",", ">", &matp->refr_color.x);
            if (strEq(tag, "refroffs"))
                matp->refr_offset = strToNum(val);
            if (strEq(tag, "refrior"))
                matp->refr_ior = strToNum(val);
            if (strEq(tag, "reframt"))
                matp->refr_amount = strToNum(val);
        } break;
        case M_RENDER:
            if (strEq(tag, "width"))
                m_w = strToNum(val);
            if (strEq(tag, "height"))
                m_h = strToNum(val);
            if (strEq(tag, "samples"))
                m_max_samples = strToNum(val);
            if (strEq(tag, "backclr")) {
                strToVec3(val, "<", ",", ">", &vec.x);
                gvdb.getScene()->SetBackgroundClr(vec.x, vec.y, vec.z, 1.0);
            }
            if (strEq(tag, "envmap"))
                m_envfile = val;
            if (strEq(tag, "outpath"))
                m_outpath = val;
            if (strEq(tag, "outfile"))
                m_outfile = val;

            break;
        case M_VOLUME: {
            nvdb::Scene *scn = gvdb.getScene();
            if (strEq(tag, "scale") && m_renderscale == 0)
                m_renderscale = strToNum(val);
            if (strEq(tag, "steps")) {
                strToVec3(val, "<", ",", ">", &vec.x);
                scn->SetSteps(vec.x, vec.y, vec.z);
            }
            if (strEq(tag, "extinct")) {
                strToVec3(val, "<", ",", ">", &vec.x);
                scn->SetExtinct(vec.x, vec.y, vec.z);
            }
            if (strEq(tag, "range")) {
                strToVec3(val, "<", ",", ">", &vec.x);
                scn->SetVolumeRange(vec.x, vec.y, vec.z);
            }
            if (strEq(tag, "cutoff")) {
                strToVec3(val, "<", ",", ">", &vec.x);
                scn->SetCutoff(vec.x, vec.y, vec.z);
            }
            if (strEq(tag, "smooth"))
                m_smooth = strToNum(val);
            if (strEq(tag, "smoothp")) {
                strToVec3(val, "<", ",", ">", &vec.x);
                m_smoothp = vec;
            }
        } break;
        case M_CAMERA: {
            Camera3D *cam = gvdb.getScene()->getCamera();
            if (strEq(tag, "angs")) {
                strToVec3(val, "<", ",", ">", &vec.x);
                cam->setAng(vec);
            }
            if (strEq(tag, "target")) {
                strToVec3(val, "<", ",", ">", &vec.x);
                vec *= m_renderscale;
                cam->setToPos(vec.x, vec.y, vec.z);
            }
            if (strEq(tag, "dist")) {
                cam->setDist(strToNum(val) * m_renderscale);
            }
            if (strEq(tag, "fov")) {
                cam->setFov(strToNum(val));
            }
            cam->setOrbit(cam->getAng(), cam->getToPos(), cam->getOrbitDist(), cam->getDolly());
        } break;
        case M_LIGHT: {
            Light *lgt = gvdb.getScene()->getLight();
            if (strEq(tag, "angs")) {
                strToVec3(val, "<", ",", ">", &vec.x);
                lgt->setAng(vec);
            }
            if (strEq(tag, "target")) {
                strToVec3(val, "<", ",", ">", &vec.x);
                vec *= m_renderscale;
                lgt->setToPos(vec.x, vec.y, vec.z);
            }
            if (strEq(tag, "dist")) {
                lgt->setDist(strToNum(val) * m_renderscale);
            }
            if (strEq(tag, "fov")) {
                lgt->setFov(strToNum(val));
            }
            lgt->setOrbit(lgt->getAng(), lgt->getToPos(), lgt->getOrbitDist(), lgt->getDolly());
        } break;
        case M_MODEL: {
            int id = model_list.size() - 1;
            if (strEq(tag, "path"))
                strncpy(model_list[id].fpath, val.c_str(), 1024);
            if (strEq(tag, "file"))
                strncpy(model_list[id].fname, val.c_str(), 1024);
            if (strEq(tag, "mat"))
                model_list[id].mat = strToNum(val);
            if (strEq(tag, "scale"))
                model_list[id].scal = strToNum(val);
            if (strEq(tag, "offset")) {
                strToVec3(val, "<", ",", ">", &vec.x);
                model_list[id].offs = vec;
            }
        } break;
    };
}

void Sample::parse_scene(std::string fname) {
    int mode = M_GLOBAL;

    char fn[1024];
    strcpy(fn, fname.c_str());
    char fpath[1024];
    if (!gvdb.FindFile(fn, fpath)) {
        printf("Error: Cannot find scene file %s\n", fname.c_str());
    }
    FILE *fp = fopen(fpath, "rt");
    char buf[2048];
    std::string lin, tag;
    Vector3DF vec;

    while (!feof(fp)) {
        fgets(buf, 2048, fp);
        lin = buf;

        if (lin.find("points") == 0) {
            m_pnton = true;
            mode = M_POINTS;
        }
        if (lin.find("polys") == 0) {
            m_polyon = true;
            mode = M_POLYS;
        }
        if (lin.find("light") == 0)
            mode = M_LIGHT;
        if (lin.find("camera") == 0)
            mode = M_CAMERA;
        if (lin.find("global") == 0)
            mode = M_GLOBAL;
        if (lin.find("render") == 0)
            mode = M_RENDER;
        if (lin.find("volume") == 0)
            mode = M_VOLUME;
        if (lin.find("material") == 0) {
            mode = M_MATERIAL;
            add_material(false);
        }
        if (lin.find("model") == 0) {
            mode = M_MODEL;
            add_model();
        }

        tag = strTrim(strSplit(lin, ":"));
        lin = strTrim(lin);
        if (tag.length() > 0 && lin.length() > 0)
            parse_value(mode, tag, lin);
    }
    fclose(fp);
}

void Sample::on_arg(std::string arg, std::string val) {
    if (arg.compare("-in") == 0) {
        m_infile = val;
        nvprintf("Input scene file: %s\n", m_infile.c_str());
    }
    else if (arg.compare("-p2g-algorithm") == 0) {
        if (val.compare("scatter") == 0) {
            m_p2g_algorithm = SCATTER;
            nvprintf("P2G algorithm: scatter\n");
        } else if (val.compare("gather") == 0) {
            m_p2g_algorithm = GATHER;
            nvprintf("P2G algorithm: gather\n");
        } else {
            m_p2g_algorithm = SCATTER_REDUCE;
            nvprintf("P2G algorithm: scatter_reduce\n");
        }
    }
    else if (arg.compare("-iteration-limit") == 0) {
        m_iteration_limit = strToNum(val);
        nvprintf("Iteration limit: %d\n", m_iteration_limit);
    }
    else if (arg.compare("-frame-limit") == 0) {
        m_frame_limit = strToNum(val);
        nvprintf("Frame limit: %d\n", m_frame_limit);
    }
    else if (arg.compare("-scale") == 0) {
        m_renderscale = 1.0 / strToNum(val);
        nvprintf("Render scale: %f\n", m_renderscale);
    }
    else if (arg.compare("-flag") == 0) {
        if (val.compare("info") == 0) {
            m_info = true;
            nvprintf("Using flag: info\n");
        }
        else if (val.compare("p2g-only") == 0) {
            m_p2g_only = true;
            nvprintf("Using flag: p2g-only\n"); // TODO: implement
        }
    }
}

bool Sample::init() {
    m_w = getWidth(); // window width & height
    m_h = getHeight();
    mouse_down = -1;
    gl_screen_tex = -1;
    m_show_topo = false;
    m_radius = 1;
    m_origin = Vector3DF(0, 0, 0);
    m_shade_style = 5;

    m_max_samples = 1;
    m_envfile = "";
    m_outpath = "";
    m_outfile = "img%04d.png";

    m_sample = 0;
    m_save_png = true;
    m_render_optix = true;
    m_smooth = 0;
    m_smoothp.Set(0, 0, 0);

    m_pnton = false; // point time series
    m_pntmat = 0;
    m_fstep = 1;

    m_polyon = false; // polygonal time series
    m_pframe = 0;
    m_pfstep = 1;
    m_pscale = 1.0;
    m_poffset = Vector3DF(0, 0, 0);
    m_polymat = 0;

    simulationFPS = 60.0; // Simulation output FPS configuration
    elapsedTime = 0.0;
    deltaTime = 1e-4;
    init2D("arial");

    // Initialize Optix Scene
    if (m_render_optix) {
        optx.InitializeOptix(m_w, m_h);
    }

    gvdb.SetDebug(true); // DEBUG
    gvdb.SetVerbose(true); // DEBUG
    gvdb.SetProfile(false, true);
    gvdb.SetCudaDevice(m_render_optix ? GVDB_DEV_CURRENT : GVDB_DEV_FIRST);
    gvdb.Initialize();
    gvdb.StartRasterGL();
    gvdb.AddPath(ASSET_PATH);

    // Default Camera
    Camera3D *cam = new Camera3D;
    cam->setFov(50.0);
    cam->setNearFar(1, 10000);
    cam->setOrbit(Vector3DF(50, 30, 0), Vector3DF(128, 128, 128), 1400, 1.0);
    gvdb.getScene()->SetCamera(cam);

    // Default Light
    Light *lgt = new Light;
    lgt->setOrbit(Vector3DF(0, 40, 0), Vector3DF(128, 128, 128), 2000, 1.0);
    gvdb.getScene()->SetLight(0, lgt);

    // Default volume params
    gvdb.getScene()->SetSteps(0.1f, 16, 0.1f);          // Set raycasting steps
    gvdb.getScene()->SetExtinct(-1.0f, 1.1f, 0.0f);     // Set volume extinction
    gvdb.getScene()->SetVolumeRange(0.0f, -1.0f, 3.0f); // Set volume value range
    gvdb.getScene()->SetCutoff(0.005f, 0.001f, 0.0f);
    gvdb.getScene()->SetBackgroundClr(0.1f, 0.2f, 0.4f, 1.0);

    // Parse scene file
    if (m_render_optix)
        ClearOptix();
    parse_scene(m_infile);

    // Add render buffer
    nvprintf("Output buffer: %d x %d\n", m_w, m_h);
    gvdb.AddRenderBuf(0, m_w, m_h, 4);

    // Resize window
    resize_window(m_w, m_h);

    // Create opengl texture for display
    glViewport(0, 0, m_w, m_h);
    createScreenQuadGL(&gl_screen_tex, m_w, m_h);

    // Configure
    gvdb.Configure(3, 3, 3, 3, 3); // Brick size fixed at 8x8x8 (last parameter)
    gvdb.SetChannelDefault(32, 32, 1); // Default atlas dimension to allocate in number of bricks

    // Level set channel for rendering (texture channel with apron size 1)
    // Positive value is outside material, negative value is inside. Background initialized to 3.0
    gvdb.AddChannel(0, T_FLOAT, 1, F_LINEAR, F_CLAMP, Vector3DI(0, 0, 0), true, Vector4DF(3.0, 0.0, 0.0, 0.0));

    // Momentum/velocity channels
    gvdb.AddChannel(1, T_FLOAT, 1, F_LINEAR, F_CLAMP, Vector3DI(0, 0, 0), false, Vector4DF(0.0, 0.0, 0.0, 0.0));
    gvdb.AddChannel(2, T_FLOAT, 1, F_LINEAR, F_CLAMP, Vector3DI(0, 0, 0), false, Vector4DF(0.0, 0.0, 0.0, 0.0));
    gvdb.AddChannel(3, T_FLOAT, 1, F_LINEAR, F_CLAMP, Vector3DI(0, 0, 0), false, Vector4DF(0.0, 0.0, 0.0, 0.0));

    // Force channels
    gvdb.AddChannel(4, T_FLOAT, 1, F_LINEAR, F_CLAMP, Vector3DI(0, 0, 0), false, Vector4DF(0.0, 0.0, 0.0, 0.0));
    gvdb.AddChannel(5, T_FLOAT, 1, F_LINEAR, F_CLAMP, Vector3DI(0, 0, 0), false, Vector4DF(0.0, 0.0, 0.0, 0.0));
    gvdb.AddChannel(6, T_FLOAT, 1, F_LINEAR, F_CLAMP, Vector3DI(0, 0, 0), false, Vector4DF(0.0, 0.0, 0.0, 0.0));

    // Mass density channel
    gvdb.AddChannel(7, T_FLOAT, 1, F_LINEAR, F_CLAMP, Vector3DI(0, 0, 0), false, Vector4DF(0.0, 0.0, 0.0, 0.0));

    // Initialize GUIs
    start_guis(m_w, m_h);

    clear_gvdb();

    // Load input data
    if (m_pnton)
        load_points(m_pntpath, m_pntfile, m_frame);
    if (m_polyon)
        load_polys(m_polypath, m_polyfile, m_pframe, m_pscale, m_poffset, m_polymat);

    // Rebuild the Optix scene graph with GVDB
    if (m_render_optix)
        RebuildOptixGraph(SHADE_LEVELSET);

    render_update();

    return true;
}

void Sample::reshape(int w, int h) {
    // Resize the opengl screen texture
    glViewport(0, 0, w, h);
    createScreenQuadGL(&gl_screen_tex, w, h);

    // Resize the GVDB render buffers
    gvdb.ResizeRenderBuf(0, w, h, 4);

    // Resize OptiX buffers
    if (m_render_optix)
        optx.ResizeOutput(w, h);

    // Resize 2D UI
    start_guis(w, h);

    postRedisplay();
}

void Sample::load_points(std::string pntpath, std::string pntfile, int frame) {
    std::string path;
    if (pntpath.empty()) {
        char filepath[1024];
        gvdb.FindFile(pntfile, filepath);
        path = std::string(filepath);
    } else {
        path = pntpath + pntfile;
    }

    std::cout << "Reading particles from " << path << std::endl;

    float particleInitialMass;

    std::ifstream fin;
    fin.open(path.c_str());

    fin >> m_numpnts; // Number of particles
    fin >> m_particleInitialVolume; // Initial volume of one particle, (m^3)
    fin >> particleInitialMass; // Mass of one particle (kg)

    gvdb.AllocData(m_particlePositions, m_numpnts, sizeof(Vector3DF), true);
    gvdb.AllocData(m_particleMasses, m_numpnts, sizeof(float), true);
    gvdb.AllocData(m_particleVelocities, m_numpnts, sizeof(float) * 3, true);
    gvdb.AllocData(m_particleDeformationGradients, m_numpnts, sizeof(float) * 9, true);
    gvdb.AllocData(m_particleAffineStates, m_numpnts, sizeof(float) * 9, true);

    // Particle positions in grid units (cm)
    Vector3DF *particlesInput = (Vector3DF*) m_particlePositions.cpu;
    for (int i = 0; i < m_numpnts; i++) {
        fin >> particlesInput[i].x >> particlesInput[i].y >> particlesInput[i].z;
    }

    fin.close();

    // Initialize particle data
    for (int i = 0; i < m_numpnts; i++) {
        // Initial particle mass
        *(((float*) m_particleMasses.cpu) + i) = particleInitialMass;

        // Velocity
        float *velocity = ((float *)m_particleVelocities.cpu) + i * 3;
        velocity[0] = 0.0;
        velocity[1] = 0.0;
        velocity[2] = 0.0;

        // Deformation gradient (initialize to identity matrix)
        float *deformationGradient = ((float *)m_particleDeformationGradients.cpu) + i * 9;
        deformationGradient[0] = 1.0;
        deformationGradient[1] = 0.0;
        deformationGradient[2] = 0.0;
        deformationGradient[3] = 0.0;
        deformationGradient[4] = 1.0;
        deformationGradient[5] = 0.0;
        deformationGradient[6] = 0.0;
        deformationGradient[7] = 0.0;
        deformationGradient[8] = 1.0;

        // APIC affine state (initialize to zero matrix)
        float *affineState = ((float *)m_particleAffineStates.cpu) + i * 9;
        affineState[0] = 0.0;
        affineState[1] = 0.0;
        affineState[2] = 0.0;
        affineState[3] = 0.0;
        affineState[4] = 0.0;
        affineState[5] = 0.0;
        affineState[6] = 0.0;
        affineState[7] = 0.0;
        affineState[8] = 0.0;
    }

    // Commit particle data to GPU
    gvdb.CommitData(m_particlePositions);
    gvdb.CommitData(m_particleMasses);
    gvdb.CommitData(m_particleVelocities);
    gvdb.CommitData(m_particleDeformationGradients);
    gvdb.CommitData(m_particleAffineStates);

    // Set points for GVDB
    gvdb.SetPoints(m_particlePositions, m_particleMasses, m_particleVelocities,
                   m_particleDeformationGradients, m_particleAffineStates);

    printf("Read %d particles.\n", m_numpnts);
}

void Sample::load_polys(std::string polypath, std::string polyfile, int frame, float pscale,
                        Vector3DF poffs, int pmat) {
    bool bFirst = false;
    char fmt[1024], fpath[1024];

    Model *m = gvdb.getScene()->getModel(0);

    // get filename
    sprintf(fmt, "%s%s", polypath.c_str(), polyfile.c_str());
    sprintf(fpath, fmt, frame);

    nvprintf("Load polydata from %s...", fpath);

    // create new model if needed
    if (m == 0x0) {
        bFirst = true;
        m = gvdb.getScene()->AddModel();
    }

    // load model
    gvdb.getScene()->LoadModel(m, fpath, pscale, poffs.x, poffs.y, poffs.z);
    gvdb.CommitGeometry(0);

    nvprintf(" Done.\n");
}

void Sample::ReportMemory() {
    std::vector<std::string> outlist;
    gvdb.MemoryUsage("gvdb", outlist);
    for (int n = 0; n < outlist.size(); n++)
        nvprintf("%s", outlist[n].c_str());
}

void Sample::clear_gvdb() {
    // Clear
    DataPtr temp;
    gvdb.SetPoints(temp, temp, temp, temp, temp);
    gvdb.CleanAux();
}

float getEventDuration(cudaEvent_t start, cudaEvent_t end) {
    cudaEventSynchronize(end);
    float duration = 0.0; // In milliseconds
    cudaEventElapsedTime(&duration, start, end);
    return duration;
}

void Sample::render_update() {
    if (m_frame_limit && (m_frame >= m_frame_limit)) {
        printf("\nReached frame limit, stopping...\n");
        m_active = false;
        return;
    }

    if (!m_pnton)
        return;

    cuProfilerStart();

    printf("\n[Frame %d] \n", m_frame);

    if (m_p2g_only) {
        printf("  P2G level set... ");

        // Rebuild GVDB Render topology
        PERF_PUSH("Dynamic Topology");
        gvdb.RebuildTopology(m_numpnts, 2.0, m_origin); // Allocate bricks so that all neighboring 3x3x3 voxels of a particle is covered
        gvdb.FinishTopology(false, true); // false. no commit pool	false. no compute bounds
        gvdb.UpdateAtlas();
        PERF_POP();

        int levelSetChannel = 0;
        float radius = 1.0;
        Vector3DF offset(0.0, 0.0, 0.0);

        cudaEvent_t p2gStart, p2gEnd;
        cudaEventCreate(&p2gStart);
        cudaEventCreate(&p2gEnd);

        cudaEventRecord(p2gStart);
        if (m_p2g_algorithm == SCATTER) {
            gvdb.ClearChannel(1);
            gvdb.ScatterLevelSet(m_numpnts, radius, offset, 1);
            gvdb.CopyLinearChannelToTextureChannel(levelSetChannel, 1);
        } else if (m_p2g_algorithm == GATHER) {
            int scPntLen = 0;
            int subcellSize = 4;
            gvdb.InsertPointsSubcell(subcellSize, m_numpnts, m_radius, m_origin, scPntLen);
            gvdb.GatherLevelSet(subcellSize, m_numpnts, radius, offset, scPntLen, levelSetChannel, -1, false);
        } else {
            gvdb.ClearChannel(1);
            gvdb.ScatterReduceLevelSet(m_numpnts, radius, offset, 1);
            gvdb.CopyLinearChannelToTextureChannel(levelSetChannel, 1);
        }
        cudaEventRecord(p2gEnd);
        float p2gDuration = getEventDuration(p2gStart, p2gEnd);

        gvdb.UpdateApron(levelSetChannel, 3.0f);

        m_iteration++;
        printf("OK (%f ms)\n", p2gDuration);

        cudaEventDestroy(p2gStart);
        cudaEventDestroy(p2gEnd);
    } else {
        printf("  MPM... ");

        cudaEvent_t frameStart, frameEnd;
        cudaEvent_t topologyStart, topologyEnd, p2gStart, p2gEnd;
        cudaEvent_t gridUpdateStart, gridUpdateEnd, g2pStart, g2pEnd;
        cudaEventCreate(&frameStart);
        cudaEventCreate(&frameEnd);
        cudaEventCreate(&topologyStart);
        cudaEventCreate(&topologyEnd);
        cudaEventCreate(&p2gStart);
        cudaEventCreate(&p2gEnd);
        cudaEventCreate(&gridUpdateStart);
        cudaEventCreate(&gridUpdateEnd);
        cudaEventCreate(&g2pStart);
        cudaEventCreate(&g2pEnd);
        float topologyFrameDuration = 0.0;
        float p2gFrameDuration = 0.0;
        float gridUpdateFrameDuration = 0.0;
        float g2pFrameDuration = 0.0;

        cudaEventRecord(frameStart);

        float frameTimeElapsed = 0.0;
        float frameTimeTarget = 1.0 / simulationFPS;
        int frameIteration = 0;

        while (frameTimeElapsed < frameTimeTarget && m_active) {
            if (m_iteration_limit && (m_iteration >= m_iteration_limit)) {
                printf("\nReached iteration limit, stopping...\n");
                m_active = false;
                break;
            }

            // Rebuild GVDB Render topology
            PERF_PUSH("Dynamic Topology");
            cudaEventRecord(topologyStart);
            gvdb.RebuildTopology(m_numpnts, 2.0, m_origin); // Allocate bricks so that all neighboring 3x3x3 voxels of a particle is covered
            gvdb.FinishTopology(false, true); // false. no commit pool	false. no compute bounds
            gvdb.UpdateAtlas();
            cudaEventRecord(topologyEnd);
            topologyFrameDuration += getEventDuration(topologyStart, topologyEnd);
            PERF_POP();

            // Gather points to level set
            PERF_PUSH("MPM");

            // P2G
            cudaEventRecord(p2gStart);
            gvdb.ClearChannel(1);
            gvdb.ClearChannel(2);
            gvdb.ClearChannel(3);
            gvdb.ClearChannel(4);
            gvdb.ClearChannel(5);
            gvdb.ClearChannel(6);
            gvdb.ClearChannel(7);
            if (m_p2g_algorithm == SCATTER) {
                gvdb.P2G_ScatterAPIC(m_numpnts, m_particleInitialVolume, 7, 1, 4);
            } else if (m_p2g_algorithm == GATHER) {
                gvdb.P2G_GatherAPIC(m_numpnts, m_particleInitialVolume, 7, 1, 4);
            } else {
                gvdb.P2G_ScatterReduceAPIC(m_numpnts, m_particleInitialVolume, 7, 1, 4);
            }
            cudaEventRecord(p2gEnd);
            p2gFrameDuration += getEventDuration(p2gStart, p2gEnd);

            // Add external forces, handle collisions, update grid velocity
            cudaEventRecord(gridUpdateStart);
            gvdb.MPM_GridUpdate(deltaTime, 7, 1, 4);
            cudaEventRecord(gridUpdateEnd);
            gridUpdateFrameDuration += getEventDuration(gridUpdateStart, gridUpdateEnd);

            // G2P and particle advection
            cudaEventRecord(g2pStart);
            gvdb.G2P_GatherAPIC(m_numpnts, deltaTime, 1);
            cudaEventRecord(g2pEnd);
            g2pFrameDuration += getEventDuration(g2pStart, g2pEnd);

            // Calculate delta time based on maximum particle speeds
            gvdb.GetMinMaxVel(m_numpnts);
            Vector3DF cellDimension = Vector3DF(gvdb.getRange(0)) * gvdb.mVoxsize / Vector3DF(gvdb.getRes3DI(0));
            Vector3DF maxParticleSpeeds(
                gvdb.mVelMax.x > -gvdb.mVelMin.x ? gvdb.mVelMax.x : -gvdb.mVelMin.x,
                gvdb.mVelMax.y > -gvdb.mVelMin.y ? gvdb.mVelMax.y : -gvdb.mVelMin.y,
                gvdb.mVelMax.z > -gvdb.mVelMin.z ? gvdb.mVelMax.z : -gvdb.mVelMin.z
            );
            float maxParticleSpeed = maxParticleSpeeds.x > maxParticleSpeeds.y
                ? (maxParticleSpeeds.x > maxParticleSpeeds.z ? maxParticleSpeeds.x : maxParticleSpeeds.z)
                : (maxParticleSpeeds.y > maxParticleSpeeds.z ? maxParticleSpeeds.y : maxParticleSpeeds.z);
            maxParticleSpeed *= 100.0; // Convert m/s to cm/s (grid units use cm)
            if (maxParticleSpeed < 1e-6) maxParticleSpeed = 1e-6;
            float calculatedDeltaTime = 0.01 * (cellDimension.x / maxParticleSpeed);

            /*
            // DEBUG
            printf("Max speeds: %f %f %f\n", maxParticleSpeeds.x, maxParticleSpeeds.y, maxParticleSpeeds.z);
            printf("Calculated delta time: %f\n", calculatedDeltaTime);
            */

            // Update delta time based on calculation delta time and remaining time to next frame
            if (calculatedDeltaTime > 1e-4) calculatedDeltaTime = 1e-4; // Limit delta time maximum
            if (frameTimeElapsed + calculatedDeltaTime > frameTimeTarget) {
                deltaTime = frameTimeTarget - frameTimeElapsed;
            } else if (frameTimeElapsed + 1.8*calculatedDeltaTime > frameTimeTarget) {
                deltaTime = (frameTimeTarget - frameTimeElapsed) / 2.0;
            } else {
                deltaTime = calculatedDeltaTime;
            }

            PERF_POP();

            elapsedTime += deltaTime;
            frameTimeElapsed += deltaTime;
            m_iteration++;
            frameIteration++;
        }
        cudaEventRecord(frameStart);
        float frameDuration = getEventDuration(frameStart, frameEnd);

        printf(
            "OK (average dt: %f s, %d MPM iterations, total simulated time: %f s)\n",
            frameIteration ? frameTimeElapsed / (float) frameIteration : 0,
            frameIteration, elapsedTime
        );
        printf(
            "    Topology rebuild : %f ms\n    P2G              : %f ms\n    Grid update      : %f ms\n    G2P              : %f ms\n    Frame total      : %f ms\n",
            topologyFrameDuration, p2gFrameDuration, gridUpdateFrameDuration, g2pFrameDuration, frameDuration
        );

        // Compute level set for render
        cudaEvent_t levelSetStart, levelSetEnd;
        cudaEventCreate(&levelSetStart);
        cudaEventCreate(&levelSetEnd);
        printf("  Computing level set... ");
        cudaEventRecord(levelSetStart);
        gvdb.ConvertLinearMassChannelToTextureLevelSetChannel(0, 7);
        gvdb.UpdateApron(0, 3.0f);
        cudaEventRecord(levelSetEnd);
        printf("OK (%f ms)\n", getEventDuration(levelSetStart, levelSetEnd));

        cudaEventDestroy(frameStart);
        cudaEventDestroy(frameEnd);
        cudaEventDestroy(topologyStart);
        cudaEventDestroy(topologyEnd);
        cudaEventDestroy(p2gStart);
        cudaEventDestroy(p2gEnd);
        cudaEventDestroy(gridUpdateStart);
        cudaEventDestroy(gridUpdateEnd);
        cudaEventDestroy(g2pStart);
        cudaEventDestroy(g2pEnd);
        cudaEventDestroy(levelSetStart);
        cudaEventDestroy(levelSetEnd);
    }

    if (m_render_optix) {
        PERF_PUSH("Update OptiX");
        optx.UpdateVolume(&gvdb); // GVDB topology has changed
        PERF_POP();
    }

    cuProfilerStop();

    // Print detailed info when rendering every frame
    if (m_info) {
        printf("  Info:\n");
        ReportMemory();
        gvdb.Measure(true);
    }
}

void Sample::render_frame() {
    // Render frame
    gvdb.getScene()->SetCrossSection(m_origin, Vector3DF(0, 0, -1));

    int sh;
    switch (m_shade_style) {
        case 0:
            sh = SHADE_OFF;
            break;
        case 1:
            sh = SHADE_VOXEL;
            break;
        case 2:
            sh = SHADE_EMPTYSKIP;
            break;
        case 3:
            sh = SHADE_SECTION3D;
            break;
        case 4:
            sh = SHADE_VOLUME;
            break;
        case 5:
            sh = SHADE_LEVELSET;
            break;
    };

    if (m_render_optix) {
        // OptiX render
        PERF_PUSH("Raytrace");
        optx.Render(&gvdb, SHADE_LEVELSET, 0);
        PERF_POP();
        PERF_PUSH("ReadToGL");
        optx.ReadOutputTex(gl_screen_tex);
        PERF_POP();
    } else {
        // CUDA render
        PERF_PUSH("Raytrace");
        gvdb.Render(sh, 0, 0);
        PERF_POP();
        PERF_PUSH("ReadToGL");
        gvdb.ReadRenderTexGL(0, gl_screen_tex);
        PERF_POP();
    }
    renderScreenQuadGL(gl_screen_tex); // Render screen-space quad with texture
}

void Sample::draw_topology() {
    Vector3DF clrs[10];
    clrs[0] = Vector3DF(0, 0, 1);          // blue
    clrs[1] = Vector3DF(0, 1, 0);          // green
    clrs[2] = Vector3DF(1, 0, 0);          // red
    clrs[3] = Vector3DF(1, 1, 0);          // yellow
    clrs[4] = Vector3DF(1, 0, 1);          // purple
    clrs[5] = Vector3DF(0, 1, 1);          // aqua
    clrs[6] = Vector3DF(1, 0.5, 0);        // orange
    clrs[7] = Vector3DF(0, 0.5, 1);        // green-blue
    clrs[8] = Vector3DF(0.7f, 0.7f, 0.7f); // grey

    VolumeGVDB *g = &gvdb;
    Vector3DF bmin, bmax;
    Node *node;

    Camera3D *cam = gvdb.getScene()->getCamera();
    start3D(cam);
    for (int lev = 0; lev < 5; lev++) { // draw all levels
        int node_cnt = g->getNumTotalNodes(lev);
        for (int n = 0; n < node_cnt; n++) { // draw all nodes at this level
            node = g->getNodeAtLevel(n, lev);
            if (!int(node->mFlags))
                continue;

            bmin = g->getWorldMin(node); // get node bounding box
            bmax = g->getWorldMax(node); // draw node as a box
            drawBox3D(bmin.x, bmin.y, bmin.z, bmax.x, bmax.y, bmax.z, clrs[lev].x, clrs[lev].y,
                      clrs[lev].z, 1);
        }
    }
    end3D();
}

void Sample::draw_points() {
    Vector3DF *fpos = (Vector3DF*) m_particlePositions.cpu;

    Vector3DF p1, p2;
    Vector3DF c;

    Camera3D *cam = gvdb.getScene()->getCamera();
    start3D(cam);
    for (int n = 0; n < m_numpnts; n++) {
        p1 = *fpos++;
        p2 = p1 + Vector3DF(0.01, 0.01, 0.01);
        c = p1 / Vector3DF(256.0, 256, 256);
        drawLine3D(p1.x, p1.y, p1.z, p2.x, p2.y, p2.z, c.x, c.y, c.z, 1);
    }
    end3D();
}

Vector3DF interp(Vector3DF a, Vector3DF b, float t) {
    return Vector3DF(a.x + t * (b.x - a.x), a.y + t * (b.y - a.y), a.z + t * (b.z - a.z));
}

void Sample::display() {
    // Update sample convergence
    if (m_render_optix)
        optx.SetSample(m_frame, m_sample);

    clearScreenGL();

    // Render frame
    render_frame();

    if (m_sample % 8 == 0 && m_sample > 0) {
        int pct = (m_sample * 100) / m_max_samples;
        nvprintf("%d%%%% ", pct);
    } else if (m_sample == 0) {
        nvprintf("  Rendering... (samples: %d) ", m_max_samples);
    }

    if (++m_sample >= m_max_samples) {
        m_sample = 0;
        nvprintf("OK\n");

        if (m_save_png && m_render_optix) {
            // Save current frame to PNG
            char png_name[1024];
            char pfmt[1024];
            sprintf(pfmt, "%s%s", m_outpath.c_str(), m_outfile.c_str());
            sprintf(png_name, pfmt, m_frame);
            std::cout << "  Saving png to " << png_name << "... ";
            optx.SaveOutput(png_name);
            std::cout << "OK\n";
        }

        m_frame += m_fstep;

        if (m_polyon) {
            m_pframe += m_pfstep;
            load_polys(m_polypath, m_polyfile, m_pframe, m_pscale, m_poffset, m_polymat);
            if (m_render_optix)
                optx.UpdatePolygons();
        }
        render_update();
    }

    /*glDisable(GL_DEPTH_TEST);
        glClearDepth(1.0);
        glClear(GL_DEPTH_BUFFER_BIT);

        if ( m_show_points) draw_points ();
        if ( m_show_topo ) draw_topology ();

        draw3D();
        drawGui(0);
        draw2D(); */

    postRedisplay(); // Post redisplay since simulation is continuous
}

void Sample::motion(int x, int y, int dx, int dy) {
    // Get camera for GVDB Scene
    Camera3D *cam = gvdb.getScene()->getCamera();
    Light *lgt = gvdb.getScene()->getLight();
    bool shift = (getMods() & NVPWindow::KMOD_SHIFT); // Shift-key to modify light

    switch (mouse_down) {
        case NVPWindow::MOUSE_BUTTON_LEFT: {
            // Adjust orbit angles
            Vector3DF angs = (shift ? lgt->getAng() : cam->getAng());
            angs.x += dx * 0.2f;
            angs.y -= dy * 0.2f;
            if (shift)
                lgt->setOrbit(angs, lgt->getToPos(), lgt->getOrbitDist(), lgt->getDolly());
            else
                cam->setOrbit(angs, cam->getToPos(), cam->getOrbitDist(), cam->getDolly());
            m_sample = 0;
        } break;

        case NVPWindow::MOUSE_BUTTON_MIDDLE: {
            // Adjust target pos
            cam->moveRelative(float(dx) * cam->getOrbitDist() / 1000,
                              float(-dy) * cam->getOrbitDist() / 1000, 0);
            m_sample = 0;
        } break;

        case NVPWindow::MOUSE_BUTTON_RIGHT: {
            // Adjust dist
            float dist = (shift ? lgt->getOrbitDist() : cam->getOrbitDist());
            dist -= dy;
            if (shift)
                lgt->setOrbit(lgt->getAng(), lgt->getToPos(), dist, cam->getDolly());
            else
                cam->setOrbit(cam->getAng(), cam->getToPos(), dist, cam->getDolly());
            m_sample = 0;
        } break;
    }
    /*if (m_sample == 0) {
        nvprintf("cam ang: %f %f %f\n", cam->getAng().x, cam->getAng().y, cam->getAng().z);
        nvprintf("cam dst: %f\n", cam->getOrbitDist());
        nvprintf("cam to:  %f %f %f\n", cam->getToPos().x, cam->getToPos().y, cam->getToPos().z);
        nvprintf("lgt ang: %f %f %f\n\n", lgt->getAng().x, lgt->getAng().y, lgt->getAng().z);
    }*/
}

void Sample::keyboardchar(unsigned char key, int mods, int x, int y) {
    switch (key) {
        case '1':
            m_show_points = !m_show_points;
            break;
        case '2':
            m_show_topo = !m_show_topo;
            break;
    };
}

void Sample::mouse(NVPWindow::MouseButton button, NVPWindow::ButtonAction state, int mods, int x,
                   int y) {
    if (guiHandler(button, state, x, y))
        return;
    mouse_down = (state == NVPWindow::BUTTON_PRESS) ? button : -1;
}

int sample_main(int argc, const char **argv) {
    return sample_obj.run("p2g-scatter MPM", "p2g-scatter", argc, argv,
                          1280, 760, 4, 5, 30);
}

void sample_print(int argc, char const *argv) {}
