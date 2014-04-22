/*! \file msgView.cpp A GLUT-based viewer for simple geometry
    (supports STL and Wavefront OBJ files) */

// viewer widget
#include "../util/glut3D/glut3D.h"
// mini scene graph for loading the model
#include "../util/miniSG/miniSG.h"
// ospray, for rendering
#include "ospray/ospray.h"

namespace ospray {
  using std::cout;
  using std::endl;

  Ref<miniSG::Model> msgModel = NULL;
  OSPModel           ospModel = NULL;

  //! the renderer we're about to use
  std::string rendererType = "raycast_eyelight";

  std::vector<miniSG::Model *> msgAnimation;

  void error(const std::string &msg)
  {
    cout << "ospray::msgView fatal error : " << msg << endl;
    cout << endl;
    cout << "Proper usage: " << endl;
    cout << "  ./msgView <inFileName>" << endl;
    cout << endl;
    exit(1);
  }

  using ospray::glut3D::Glut3DWidget;

  /*! mini scene graph viewer widget. \internal Note that all handling
      of camera is almost exactly similar to the code in volView;
      might make sense to move that into a common class! */
  struct MSGViewer : public Glut3DWidget {
    MSGViewer(OSPModel model) 
      : Glut3DWidget(Glut3DWidget::FRAMEBUFFER_NONE),
        fb(NULL), renderer(NULL), model(model)
    {
      Assert(model && "null model handle");
      camera = ospNewCamera("perspective");
      Assert(camera != NULL && "could not create camera");
      ospSet3f(camera,"pos",-1,1,-1);
      ospSet3f(camera,"dir",+1,-1,+1);
      ospCommit(camera);
      
      renderer = ospNewRenderer(rendererType.c_str());
      if (!renderer)
        throw std::runtime_error("could not create renderer '"+rendererType+"'");
      Assert(renderer != NULL && "could not create renderer");

      PRINT(renderer);
      PRINT(model);
      ospSetParam(renderer,"world",model);
      ospSetParam(renderer,"model",model);
      ospSetParam(renderer,"camera",camera);
      ospCommit(camera);
      ospCommit(renderer);

    };
    virtual void reshape(const ospray::vec2i &newSize) 
    {
      Glut3DWidget::reshape(newSize);
      if (fb) ospFreeFrameBuffer(fb);
      fb = ospNewFrameBuffer(newSize,OSP_RGBA_I8);
      ospSetf(camera,"aspect",viewPort.aspect);
      ospCommit(camera);
    }

    virtual void display() 
    {
      if (!fb || !renderer) return;

      static int frameID = 0;

      //{
      // note that the order of 'start' and 'end' here is
      // (intentionally) reversed: due to our asynchrounous rendering
      // you cannot place start() and end() _around_ the renderframe
      // call (which in itself will not do a lot other than triggering
      // work), but the average time between ttwo calls is roughly the
      // frame rate (including display overhead, of course)
      if (frameID > 0) fps.doneRender(); 
      fps.startRender();
      //}

      ++frameID;

      if (viewPort.modified) {
        Assert2(camera,"ospray camera is null");
        // PRINT(viewPort);
        ospSetVec3f(camera,"pos",viewPort.from);
        ospSetVec3f(camera,"dir",viewPort.at-viewPort.from);
        ospSetVec3f(camera,"up",viewPort.up);
        ospSetf(camera,"aspect",viewPort.aspect);
        ospCommit(camera);
        viewPort.modified = false;
      }

      ospRenderFrame(fb,renderer);
    
      ucharFB = (uint32 *) ospMapFrameBuffer(fb);
      frameBufferMode = Glut3DWidget::FRAMEBUFFER_UCHAR;
      Glut3DWidget::display();
    
      ospUnmapFrameBuffer(ucharFB,fb);

      char title[1000];
      
      sprintf(title,"Test04: GlutWidget+ospray API rest (%f fps)",
              fps.getFPS());
      setTitle(title);
      forceRedraw();
    }

    OSPModel       model;
    OSPFrameBuffer fb;
    OSPRenderer    renderer;
    OSPCamera      camera;
    ospray::glut3D::FPSCounter fps;
  };

  void createMaterial(OSPGeometry ospMesh, 
                      OSPRenderer renderer,
                      miniSG::Material *mat)
  {
    OSPMaterial ospMat = ospNewMaterial(renderer,"OBJMaterial");
    if (!ospMat)  {
      cout << "given renderer does not know material type 'OBJMaterial'" << endl;
      return;
    }

    if (!mat) {
      cout << "WARNING: mesh does not have a material! (assigning default)" << endl;
      ospSet3f(ospMat,"Kd",1.f,0.f,0.f);
    } else {
      ospSet3fv(ospMat,"Kd",&mat->Kd.x);
      ospSet3fv(ospMat,"Ks",&mat->Ks.x);
      ospSet1f(ospMat,"Ns",mat->Ns);
      ospSet1f(ospMat,"d", mat->d);
    }

    ospCommit(ospMat);
    ospSetMaterial(ospMesh,ospMat);
    ospRelease(ospMat);
  }
  

  void msgViewMain(int &ac, const char **&av)
  {
    msgModel = new miniSG::Model;
    
    // -------------------------------------------------------
    // parse cmdline
    // -------------------------------------------------------
    cout << "msgView: starting to process cmdline arguments" << endl;
    for (int i=1;i<ac;i++) {
      const std::string arg = av[i];
      if (arg == "--renderer") {
        assert(i+1 < ac);
        rendererType = av[++i];
      } else if (arg == "--module" || arg == "--plugin") {
        assert(i+1 < ac);
        const char *moduleName = av[++i];
        cout << "loading ospray module '" << moduleName << "'" << endl;
        ospLoadModule(moduleName);
      } else if (av[i][0] == '-') {
        error("unkown commandline argument '"+arg+"'");
      } else {
        embree::FileName fn = arg;
        if (fn.ext() == "stl")
          miniSG::importSTL(*msgModel,fn);
        else if (fn.ext() == "msg")
          miniSG::importMSG(*msgModel,fn);
        else if (fn.ext() == "xml")
          miniSG::importRIVL(*msgModel,fn);
        else if (fn.ext() == "obj")
          miniSG::importOBJ(*msgModel,fn);
        else if (fn.ext() == "astl")
          miniSG::importSTL(msgAnimation,fn);
        else
          error("unrecognized file format in filename '"+arg+"'");
      }
    }
    // -------------------------------------------------------
    // done parsing
    // -------------------------------------------------------
    cout << "msgView: done parsing. found model with" << endl;
    cout << "  - num materials: " << msgModel->material.size() << endl;
    cout << "  - num meshes   : " << msgModel->mesh.size() << " ";
    int numUniqueTris = 0;
    int numInstancedTris = 0;
    for (int i=0;i<msgModel->mesh.size();i++) {
      if (i < 10)
        cout << "[" << msgModel->mesh[i]->size() << "]";
      else 
        if (i == 10) cout << "...";
      numUniqueTris += msgModel->mesh[i]->size();
    }
    cout << endl;
    cout << "  - num instances: " << msgModel->instance.size() << " ";
    for (int i=0;i<msgModel->instance.size();i++) {
      if (i < 10)
        cout << "[" << msgModel->mesh[msgModel->instance[i].meshID]->size() << "]";
      else 
        if (i == 10) cout << "...";
      numInstancedTris += msgModel->mesh[msgModel->instance[i].meshID]->size();
    }
    cout << endl;
    cout << "  - num unique triangles   : " << numUniqueTris << endl;
    cout << "  - num instanced triangles: " << numInstancedTris << endl;

    if (numInstancedTris == 0 && msgAnimation.empty()) 
      error("no (valid) input files specified - model contains no triangles");

    if (msgModel->material.empty()) {
      cout << "msgView: adding default material" << endl;
      msgModel->material.push_back(new miniSG::Material);
    }

    // -------------------------------------------------------
    // create ospray model
    // -------------------------------------------------------
    ospModel = ospNewModel();

    // code does not yet do instancing ... check that the model doesn't contain instances
    for (int i=0;i<msgModel->instance.size();i++)
      if (msgModel->instance[i] != miniSG::Instance(i))
        error("found a scene that seems to contain instances, "
              "but msgView does not yet support instancing");
    
    cout << "msgView: adding parsed geometries to ospray model" << endl;
    for (int i=0;i<msgModel->mesh.size();i++) {
      //      printf("Mesh %i/%li\n",i,msgModel->mesh.size());
      Ref<miniSG::Mesh> msgMesh = msgModel->mesh[i];

      // create ospray mesh
      OSPGeometry ospMesh = ospNewTriangleMesh();
      
      // add position array to mesh
      OSPData position = ospNewData(msgMesh->position.size(),OSP_vec3fa,
                                    &msgMesh->position[0],OSP_DATA_SHARED_BUFFER);
      ospSetData(ospMesh,"position",position);
      
      // add triangle index array to mesh
      OSPData index = ospNewData(msgMesh->triangle.size(),OSP_vec3i,
                                 &msgMesh->triangle[0],OSP_DATA_SHARED_BUFFER);
      ospSetData(ospMesh,"index",index);
      
      createMaterial(ospMesh, NULL, msgMesh->material.ptr);

      ospAddGeometry(ospModel,ospMesh);
    }
    ospCommit(ospModel);
    cout << "msgView: done creating ospray model." << endl;

    // -------------------------------------------------------
    // create viewer window
    // -------------------------------------------------------
    MSGViewer window(ospModel);
    window.create("MSGViewer: OSPRay Mini-Scene Graph test viewer");
    printf("MSG Viewer created. Press 'Q' to quit.\n");
    window.setWorldBounds(box3f(msgModel->getBBox()));
    ospray::glut3D::runGLUT();
  }
}


int main(int ac, const char **av)
{
  ospInit(&ac,av);
  ospray::glut3D::initGLUT(&ac,av);
  ospray::msgViewMain(ac,av);
}
