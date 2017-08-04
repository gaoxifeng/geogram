/*
 *  Copyright (c) 2012-2014, Bruno Levy
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *  this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *  this list of conditions and the following disclaimer in the documentation
 *  and/or other materials provided with the distribution.
 *  * Neither the name of the ALICE Project-Team nor the names of its
 *  contributors may be used to endorse or promote products derived from this
 *  software without specific prior written permission.
 * 
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 *  If you modify this software, you should include a notice giving the
 *  name of the person performing the modification, the date of modification,
 *  and the reason for such modification.
 *
 *  Contact: Bruno Levy
 *
 *     Bruno.Levy@inria.fr
 *     http://www.loria.fr/~levy
 *
 *     ALICE Project
 *     LORIA, INRIA Lorraine, 
 *     Campus Scientifique, BP 239
 *     54506 VANDOEUVRE LES NANCY CEDEX 
 *     FRANCE
 *
 */

#include "commands.h"

#include <geogram_gfx/glup_viewer/glup_viewer.h>
#include <geogram_gfx/mesh/mesh_gfx.h>

#include <geogram/mesh/mesh_repair.h>
#include <geogram/mesh/mesh_fill_holes.h>
#include <geogram/mesh/mesh_degree3_vertices.h>
#include <geogram/mesh/mesh_intersection.h>
#include <geogram/mesh/mesh_geometry.h>
#include <geogram/mesh/mesh_preprocessing.h>
#include <geogram/mesh/mesh_remesh.h>
#include <geogram/mesh/mesh_decimate.h>
#include <geogram/mesh/mesh_tetrahedralize.h>
#include <geogram/mesh/mesh_topology.h>

#include <geogram/points/co3ne.h>

#include <geogram/basic/command_line.h>
#include <geogram/basic/command_line_args.h>
#include <geogram/basic/logger.h>

namespace {
    GEO::Mesh* mesh = nil;
    GEO::MeshGfx* mesh_gfx= nil;
    bool* show_vertices = nil;
    bool* show_surface = nil;
    bool* show_volume = nil;
    bool* show_console = nil;
}

namespace {
    using namespace GEO;

/********************************************************************/

    namespace MeshCommands {
        static void begin() {
            mesh->vertices.set_double_precision();            
        }

        static void end() {
            if(mesh->vertices.nb() != 0 && mesh->cells.nb() == 0) {
                orient_normals(*mesh);
            }
            mesh->vertices.set_single_precision();
            mesh_gfx->set_mesh(mesh);
        }

        static void clear(bool are_you_sure = false) {
            if(are_you_sure) {
                mesh->clear();
                mesh->vertices.set_single_precision();
                mesh_gfx->set_mesh(mesh);
            }
        }

        static void remove_elements(
            bool vertices=false,
            bool edges=false,
            bool facets=false,
            bool cells=false,
            bool kill_isolated_vx=false
        ) {
            if(vertices) {
                mesh->clear();
            } else {
                if(facets) {
                    mesh->facets.clear();
                }
                if(edges) {
                    mesh->edges.clear();
                }
                if(cells) {
                    mesh->cells.clear();
                }
                if(kill_isolated_vx) {
                    mesh->vertices.remove_isolated();
                }
            }
            mesh_gfx->set_mesh(mesh);
        }

        static void remove_isolated_vertices(bool yes_I_am_sure = false) {
            if(yes_I_am_sure) {
                mesh->vertices.remove_isolated();
                mesh_gfx->set_mesh(mesh);
            }
        }

        static void show_statistics() {
            *show_console = true;
            mesh->show_stats("Mesh");
        }

        static void show_topology() {
            *show_console = true;
            Logger::out("MeshTopology/surface")
                << "Nb components = "
                << mesh_nb_connected_components(*mesh)
                << std::endl;
            Logger::out("MeshTopology/surface")
                << "Nb borders = "
                << mesh_nb_borders(*mesh)
                << std::endl;
            Logger::out("MeshTopology/surface")
                << "Xi = "
                << mesh_Xi(*mesh)
                << std::endl;
        }
    }
    
/********************************************************************/
    
    namespace MeshPointsCommands {
        using namespace MeshCommands;
        
        static void smooth(index_t nb_iterations=2, index_t nb_neighbors=30) {
            begin();
            if(nb_iterations != 0) {
                Co3Ne_smooth(*mesh, nb_neighbors, nb_iterations);
            }
            end();
        }

        static void reconstruct(
            double radius=5.0,
            index_t nb_iterations=0, index_t nb_neighbors=30
        ) {
            begin();
            double R = bbox_diagonal(*mesh);
            mesh_repair(*mesh, MESH_REPAIR_COLOCATE, 1e-6*R);
            radius *= 0.01 * R;
            if(nb_iterations != 0) {
                Co3Ne_smooth(*mesh, nb_neighbors, nb_iterations);
            }
            Co3Ne_reconstruct(*mesh, radius);
            mesh->vertices.set_single_precision();
            end();
            *show_vertices = false;
            *show_surface = true;
        }
    }

/********************************************************************/
    
    namespace MeshSurfaceCommands {
        using namespace MeshCommands;
        
        static void repair_surface(
            double epsilon = 1e-6,
            double min_comp_area = 0.03,
            double max_hole_area = 1e-3,
            index_t max_hole_edges = 2000,
            double max_degree3_dist = 0.0,
            bool remove_isect = false
        ) {
            begin();

            double bbox_diagonal = GEO::bbox_diagonal(*mesh);
            epsilon *= (0.01 * bbox_diagonal);
            double area = GEO::Geom::mesh_area(*mesh,3);
            min_comp_area *= area;
            max_hole_area *= area;

            mesh_repair(*mesh, GEO::MESH_REPAIR_DEFAULT, epsilon);

            if(min_comp_area != 0.0) {
                double nb_f_removed = mesh->facets.nb();
                GEO::remove_small_connected_components(*mesh, min_comp_area);
                nb_f_removed -= mesh->facets.nb();
                if(nb_f_removed != 0) {
                    GEO::mesh_repair(
                        *mesh, GEO::MESH_REPAIR_DEFAULT, epsilon
                    );
                }
            }

            if(max_hole_area != 0.0 && max_hole_edges != 0) {
                GEO::fill_holes(
                    *mesh, max_hole_area, max_hole_edges
                );
            }

            if(max_degree3_dist > 0.0) {
                max_degree3_dist *= (0.01 * bbox_diagonal);
                GEO::remove_degree3_vertices(*mesh, max_degree3_dist);
            }
        
            if(remove_isect) {
                Logger::out("Mesh") << "Removing intersections" << std::endl;
                GEO::mesh_remove_intersections(*mesh);
                Logger::out("Mesh") << "Removed intersections" << std::endl;
            }
        
            end();
        }


        static void merge_vertices(
            double epsilon=1e-6
        ) {
            begin();
            
            epsilon *= (0.01 * bbox_diagonal(*mesh));
            mesh_repair(*mesh, GEO::MESH_REPAIR_DEFAULT, epsilon);
            
            end();
        }

        static void remesh_smooth(
            index_t nb_points = 30000,
            double tri_shape_adapt = 1.0,
            double tri_size_adapt = 0.0,
            index_t normal_iter = 3,
            index_t Lloyd_iter = 5,
            index_t Newton_iter = 30,
            index_t Newton_m = 7,
            index_t LFS_samples = 10000
        ) {
            if(mesh->facets.nb() == 0) {
                Logger::err("Remesh")
                    << "mesh has no facet" << std::endl;
                return;
            }
        
            if(!mesh->facets.are_simplices()) {
                Logger::err("Remesh")
                    << "mesh need to be simplicial, use repair"
                    << std::endl;
                return;
            }
            
            begin();
            Mesh remesh;

            if(tri_shape_adapt != 0.0) {
                tri_shape_adapt *= 0.02;
                GEO::compute_normals(*mesh);
                if(normal_iter != 0) {
                    GEO::Logger::out("Nsmooth") << "Smoothing normals, "
                                                << normal_iter
                                                << " iteration(s)" << std::endl;
                    GEO::simple_Laplacian_smooth(
                        *mesh, normal_iter, true
                    );
                }
                GEO::set_anisotropy(*mesh, tri_shape_adapt);
            } else {
                mesh->vertices.set_dimension(3);
            }

            if(tri_size_adapt != 0.0) {
                GEO::compute_sizing_field(
                    *mesh, tri_size_adapt, LFS_samples
                );
            } else {
                AttributesManager& attributes =
                    mesh->vertices.attributes();
                if(attributes.is_defined("weight")) {
                    attributes.delete_attribute_store("weight");
                }
            }
        
            GEO::remesh_smooth(
                *mesh, remesh,
                nb_points, 0,
                Lloyd_iter, Newton_iter, Newton_m
            );

            MeshElementsFlags what = MeshElementsFlags(
                MESH_VERTICES | MESH_EDGES | MESH_FACETS | MESH_CELLS
            );
            mesh->clear();
            mesh->copy(remesh, true, what);
            
            end();
        }

        static void decimate(
            index_t nb_bins = 100,
            bool remove_deg3_vrtx = true,
            bool keep_borders = true,
            bool repair = true
        ) {
            begin();
            MeshDecimateMode mode = MESH_DECIMATE_DUP_F;
            if(remove_deg3_vrtx) {
                mode = MeshDecimateMode(mode | MESH_DECIMATE_DEG_3);
            }
            if(keep_borders) {
                mode = MeshDecimateMode(mode | MESH_DECIMATE_KEEP_B);
            }
            mesh_decimate_vertex_clustering(*mesh, nb_bins, mode);
            if(repair) {
                repair_surface();
            }
            end();
        }
    }

    namespace MeshShapesCommands {
        using namespace MeshCommands;

        static void create_cube(
            double x1=0, double y1=0, double z1=0,
            double x2=1, double y2=1, double z2=1
        ) {
            begin();
            Mesh& M = *mesh;
            if(M.vertices.dimension() < 3) {
                Logger::err("Mesh") << "Dimension smaller than 3"
                                    << std::endl;
                return;
            }

            index_t v0 = M.vertices.create_vertex(vec3(x1,y1,z1).data());
            index_t v1 = M.vertices.create_vertex(vec3(x1,y1,z2).data());
            index_t v2 = M.vertices.create_vertex(vec3(x1,y2,z1).data());
            index_t v3 = M.vertices.create_vertex(vec3(x1,y2,z2).data());
            index_t v4 = M.vertices.create_vertex(vec3(x2,y1,z1).data());
            index_t v5 = M.vertices.create_vertex(vec3(x2,y1,z2).data());
            index_t v6 = M.vertices.create_vertex(vec3(x2,y2,z1).data());
            index_t v7 = M.vertices.create_vertex(vec3(x2,y2,z2).data());
            
            M.facets.create_quad(v3,v7,v6,v2);
            M.facets.create_quad(v0,v1,v3,v2);
            M.facets.create_quad(v1,v5,v7,v3);
            M.facets.create_quad(v5,v4,v6,v7);
            M.facets.create_quad(v0,v4,v5,v1);
            M.facets.create_quad(v2,v6,v4,v0);

            M.facets.connect();
            end();
        }

        static void create_icosahedron() {
            begin();
            Mesh& M = *mesh;
            if(M.vertices.dimension() < 3) {
                Logger::err("Mesh") << "Dimension smaller than 3"
                                    << std::endl;
                return;
            }
            
            static double points[] = {
                0,          0.0,       1.175571,
                1.051462,   0.0,       0.5257311,
                0.3249197,  1.0,       0.5257311,
                -0.8506508, 0.618034,  0.5257311,
                -0.8506508, -0.618034, 0.5257311,
                0.3249197,  -1.0,      0.5257311,
                0.8506508,  0.618034,  -0.5257311,
                0.8506508,  -0.618034, -0.5257311,
                -0.3249197,  1.0,      -0.5257311,
                -1.051462,   0.0,      -0.5257311,
                -0.3249197, -1.0,      -0.5257311,
                0.0,         0.0,      -1.175571
            };
        
            static index_t facets[] = {
                0,1,2,
                0,2,3,
                0,3,4,
                0,4,5,
                0,5,1,
                1,5,7,
                1,7,6,
                1,6,2,
                2,6,8,
                2,8,3,
                3,8,9,
                3,9,4,
                4,9,10,
                4,10,5,
                5,10,7,
                6,7,11,
                6,11,8,
                7,10,11,
                8,11,9,
                9,11,10,
            };

            index_t first_v = M.vertices.create_vertices(12);
            for(index_t v=0; v<12; ++v) {
                Geom::mesh_vertex_ref(M,first_v+v) =
                    vec3(points[3*v], points[3*v+1], points[3*v+2]) ;
            }

            for(index_t f=0; f<20; ++f) {
                M.facets.create_triangle(
                    first_v + facets[3*f],
                    first_v + facets[3*f+1],
                    first_v + facets[3*f+2]
                );
            }
            
            M.facets.connect();
            end();
        }
    }

    namespace MeshVolumeCommands {
        using namespace MeshCommands;

        static void tet_meshing(
            bool preprocess=true,
            bool refine=true,
            double quality=1.0,
            bool verbose=false
        ) {
            if(verbose) {
                *show_console = true;
            }
            begin();
            CmdLine::set_arg("dbg:tetgen",verbose);        
            mesh->cells.clear();
            mesh->vertices.remove_isolated();
            mesh_tetrahedralize(*mesh, preprocess, refine, quality);
            if(mesh->cells.nb() != 0) {
                mesh->cells.compute_borders();
            }
            end();
            *show_volume = true;            
        }
    }
    
    
/********************************************************************/
    
}

void vorpaview_commands_init(
    GEO::Mesh& M, GEO::MeshGfx& M_gfx,
    bool& show_vertices_in, bool& show_surface_in, bool& show_volume_in,
    bool& show_console_in
) {
    GEO::CmdLine::import_arg_group("co3ne");
    GEO::CmdLine::import_arg_group("pre");
    GEO::CmdLine::import_arg_group("post");
    GEO::CmdLine::import_arg_group("remesh");
    GEO::CmdLine::import_arg_group("opt");
    GEO::CmdLine::import_arg_group("tet");
    mesh = &M;
    mesh_gfx = &M_gfx;
    show_vertices = &show_vertices_in;
    show_surface = &show_surface_in;
    show_volume = &show_volume_in;
    show_console = &show_console_in;
}

void vorpaview_commands_menus() {
    if(ImGui::BeginMenu("Points")) {
        if(ImGui::MenuItem("smooth point set")) {
            GEO::Command::set_current(
                "void smooth(                                            "
                "   index_t nb_iterations=2 [number of iterations],      "
                "   index_t nb_neighbors=30 [number of nearest neighbors]"
                ") [smoothes a pointset]",
                &MeshPointsCommands::smooth
            );
        }
        if(ImGui::MenuItem("reconstruct surface")) {
            GEO::Command::set_current(
                "void reconstruct(                                             "
                "   double radius=5.0       [search radius (in % bbox. diag.)],"
                "   index_t nb_iterations=0 [number of smoothing iterations],  "
                "   index_t nb_neighbors=30 [number of nearest neighbors]      "
                ") [reconstructs a surface from a pointset]",
                &MeshPointsCommands::reconstruct
            );
        }
        ImGui::EndMenu();
    }
    if(ImGui::BeginMenu("Surface")) {
        if(ImGui::BeginMenu("Repair")) {        
            if(ImGui::MenuItem("repair surface")) {
                GEO::Command::set_current(
                " void repair(                        "
                "   double epsilon = 1e-6 [point merging tol. (% bbox. diag.)],"
                "   double min_comp_area = 0.03                   "
                "        [for removing small cnx (% total area)], "
                "   double max_hole_area = 1e-3                   "
                "        [for filling holes (% total area)],      "
                "   index_t max_hole_edges = 2000                 "
                "        [max. nb. edges in filled hole],         "
                "   double max_degree3_dist = 0.0                 "
                "        [for removing deg3 vrtx (% bbox. diag.)],"
                "   bool remove_isect = false                     "
                "        [remove intersecting triangles]          "
                " ) [repairs a surfacic mesh]",
                &MeshSurfaceCommands::repair_surface
                );
            }
            if(ImGui::MenuItem("merge vertices")) {
                GEO::Command::set_current(
                "void merge_vertices(                                       "
                "   double epsilon=1e-6                                     "
                "     [tolerance for merging vertices (in % bbox diagonal)],"
                ") [merges the vertices that are within tolerance]          ",
                &MeshSurfaceCommands::merge_vertices                    
                );
            }
            ImGui::EndMenu();            
        }
        if(ImGui::BeginMenu("Remesh")) {
            if(ImGui::MenuItem("remesh smooth")) {
                GEO::Command::set_current(
                    "void remesh_smooth(                                      "
#ifdef GEO_OS_EMSCRIPTEN                    
                    "  index_t nb_points =  5000 [number of points in remesh],"
#else
                    "  index_t nb_points = 30000 [number of points in remesh],"
#endif                    
                    "  double tri_shape_adapt = 1.0                           "
                    "          [triangles shape adaptation],                  "
                    "  double tri_size_adapt = 0.0                            "
                    "          [triangles size adaptation],                   "
                    "  index_t normal_iter = 3 [nb normal smoothing iter.],   "
                    "  index_t Lloyd_iter = 5 [nb Lloyd iter.],               "
                    "  index_t Newton_iter = 30 [nb Newton iter.],            "
                    "  index_t Newton_m = 7 [nb Newton eval. per step],       "
                    "  index_t LFS_samples = 10000                            "
                    "    [nb samples (used if size adapt != 0)]               "
                    ")",
                &MeshSurfaceCommands::remesh_smooth                    
                );
            }
            if(ImGui::MenuItem("decimate")) {
                GEO::Command::set_current(
                    "void decimate(                                            "
                    "   index_t nb_bins = 100  [the higher-the more precise],  "
                    "   bool remove_deg3_vrtx = true [remove degree3 vertices],"
                    "   bool keep_borders = true,                              "
                    "   bool repair = true                                     "
                    ") [quick and dirty mesh decimator (vertex clustering)]",
                    &MeshSurfaceCommands::decimate
                );
            }
            ImGui::EndMenu();                        
        }
        if(ImGui::BeginMenu("Shapes")) {
            if(ImGui::MenuItem("create cube")) {
                GEO::Command::set_current(
                    "void create_cube("
                    "    double x1=0, double y1=0, double z1=0,"
                    "    double x2=1, double y2=1, double z2=1"
                    ")",
                    &MeshShapesCommands::create_cube
                );
            }
            if(ImGui::MenuItem("create icosahedron")) {
                MeshShapesCommands::create_icosahedron();
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }
    if(ImGui::BeginMenu("Volume")) {
        if(ImGui::MenuItem("tet meshing")) {
            Command::set_current(
                "void tet_meshing("
                "    bool preprocess=true [preprocesses the surface],        "
                "    bool refine=true     [insert points to improve quality],"
                "    double quality=1.0   [the smaller - the higher quality],"
                "    bool verbose=false   [enable tetgen debug messages]     "
                ") [Fills-in a closed mesh with tets, using tetgen]",
                &MeshVolumeCommands::tet_meshing
            );
        }
        ImGui::EndMenu();
    }
    if(ImGui::BeginMenu("Mesh")) {
        if(ImGui::BeginMenu("Stats")) {
            if(ImGui::MenuItem("show mesh statistics")) {
                MeshCommands::show_statistics();
            }
            if(ImGui::MenuItem("show mesh topology")) {
                MeshCommands::show_topology();
            }
            ImGui::EndMenu();
        }
        if(ImGui::MenuItem("clear")) {
            Command::set_current(
                "void clear(bool yes_I_am_sure=false) "
                "[removes all elements from the mesh]",
                &MeshCommands::clear
            );
        }
        if(ImGui::MenuItem("remove elements")) {
            Command::set_current(
                "void remove_elements(                                   "
                "    bool vertices=false   [removes everyting],          "
                "    bool edges=false      [removes mesh edges],         "
                "    bool facets=false     [removes the surfacic part],  "
                "    bool cells=false      [removes the volumetric part],"
                "    bool kill_isolated_vx=false [kill isolated vertices]"
                ") [removes mesh elements]",
                &MeshCommands::remove_elements
            );
        }
        if(ImGui::MenuItem("remove isolated vertices")) {
            Command::set_current(
                "void remove_isolated_vertices(bool yes_I_am_sure=false) "
                "[removes vertices that are not connected to any element]",
                &MeshCommands::remove_isolated_vertices
            );
        }
        ImGui::EndMenu();
    }
}

void vorpaview_commands_gui() { 
    if(GEO::Command::current() == nil) {
        return;
    }

    const int MENU_HEIGHT = 20;
    const int PANE_WIDTH = 140;
    const int CONSOLE_HEIGHT = 200;
    
    int w,h;
    glup_viewer_get_screen_size(&w, &h);

    float pane_height = float(h - MENU_HEIGHT);
    if(*show_console) {
        pane_height -= float(CONSOLE_HEIGHT);
    }

    ImGui::SetNextWindowPos(
        ImVec2(0.0f, float(MENU_HEIGHT) + pane_height/2.0f + 1.0f),
        ImGuiSetCond_Always
    );

    ImGui::SetNextWindowSize(
        ImVec2(float(PANE_WIDTH), pane_height/2.0f - 1.0f),
        ImGuiSetCond_Always
    );

    ImGui::Begin(
        "Command", NULL,
        ImGuiWindowFlags_NoResize   |
        ImGuiWindowFlags_NoMove     |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoTitleBar
    );

    if(GEO::Command::current() != nil) {
        GEO::Command::current()->do_gui();
    }
    
    ImGui::End();
}
