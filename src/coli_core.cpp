#include <isl/ctx.h>
#include <isl/aff.h>
#include <isl/set.h>
#include <isl/map.h>
#include <isl/constraint.h>
#include <isl/union_map.h>
#include <isl/union_set.h>
#include <isl/ast_build.h>
#include <isl/schedule.h>
#include <isl/schedule_node.h>

#include <coli/debug.h>
#include <coli/core.h>
#include <coli/parser.h>

#include <string>
#include <algorithm>

namespace coli
{

std::map<std::string, computation *> computations_list;
bool global::auto_data_mapping;

// Used for the generation of new variable names.
int id_counter = 0;

/**
 * Retrieve the access function of the ISL AST leaf node (which represents a
 * computation).  Store the access in computation->access.
 */
isl_ast_node *stmt_code_generator(
    isl_ast_node *node, isl_ast_build *build, void *user);

isl_ast_node *for_code_generator_after_for(
    isl_ast_node *node, isl_ast_build *build, void *user);


/**
  * Generate an isl AST for the function.
  */
void function::gen_isl_ast()
{
    // Check that time_processor representation has already been computed,
    // that the time_processor identity relation can be computed without any
    // issue and check that the access was provided.
    assert(this->get_schedule() != NULL);

    DEBUG_FCT_NAME(3);
    DEBUG_INDENT(4);

    isl_ctx *ctx = this->get_ctx();
    isl_ast_build *ast_build;

    if (this->get_context_set() == NULL)
        ast_build = isl_ast_build_alloc(ctx);
    else
        ast_build = isl_ast_build_from_context(this->get_context_set());

    isl_options_set_ast_build_atomic_upper_bound(ctx, 1);
    isl_options_get_ast_build_exploit_nested_bounds(ctx);
    ast_build = isl_ast_build_set_after_each_for(ast_build, &coli::for_code_generator_after_for, NULL);
    ast_build = isl_ast_build_set_at_each_domain(ast_build, &coli::stmt_code_generator, this);

    this->align_schedules();

    // Intersect the iteration domain with the domain of the schedule.
    isl_union_map *umap =
        isl_union_map_intersect_domain(
            isl_union_map_copy(this->get_schedule()),
            isl_union_set_copy(this->get_iteration_domain()));

    DEBUG(3, coli::str_dump("Schedule:", isl_union_map_to_str(this->get_schedule())));
    DEBUG(3, coli::str_dump("Iteration domain:", isl_union_set_to_str(this->get_iteration_domain())));
    DEBUG(3, coli::str_dump("Schedule intersect Iteration domain:", isl_union_map_to_str(umap)));
    DEBUG(3, coli::str_dump("\n"));

    this->ast = isl_ast_build_node_from_schedule_map(ast_build, umap);

    isl_ast_build_free(ast_build);

    DEBUG_INDENT(-4);
}


/**
  * A helper function to split a string.
  */
// TODO: Test this function
void split_string(std::string str, std::string delimiter,
          std::vector<std::string> &vector)
{
    size_t pos = 0;
    std::string token;
    while ((pos = str.find(delimiter)) != std::string::npos) {
        token = str.substr(0, pos);
        vector.push_back(token);
        str.erase(0, pos + delimiter.length());
    }
    token = str.substr(0, pos);
    vector.push_back(token);
}

void coli::parser::constraint::parse(std::string str)
{
    assert(str.empty() == false);

    split_string(str, "and", this->constraints);
};

void coli::parser::space::parse(std::string space)
{
    std::vector<std::string> vector;
    split_string(space, ",", vector);

    // Check if the vector has constraints
    for (int i=0; i<vector.size(); i++)
    {
        if (vector[i].find("=") != std::string::npos)
        {
            vector[i] = vector[i].erase(0, vector[i].find("=")+1);
        }
    }

    this->dimensions = vector;
}

std::string generate_new_variable_name()
{
    return "c" + std::to_string(id_counter++);
}


/**
  * Methods for the computation class.
  */
void coli::computation::tag_parallel_dimension(int par_dim)
{
    assert(par_dim >= 0);
    assert(this->get_name().length() > 0);
    assert(this->get_function() != NULL);

    this->get_function()->add_parallel_dimension(this->get_name(), par_dim);
}


void coli::computation::tag_gpu_dimensions(int dim0, int dim1)
{
    assert(dim0 >= 0);
    assert(dim1 >= 0);
    assert(dim1 == dim0 + 1);
    assert(this->get_name().length() > 0);
    assert(this->get_function() != NULL);

    this->get_function()->add_gpu_dimensions(this->get_name(), dim0, dim1);
}

void coli::computation::tag_vector_dimension(int dim)
{
    assert(dim >= 0);
    assert(this->get_name().length() > 0);
    assert(this->get_function() != NULL);

    this->get_function()->add_vector_dimension(this->get_name(), dim);
}

void computation::dump_iteration_domain() const
{
    if (ENABLE_DEBUG)
    {
        isl_set_dump(this->get_iteration_domain());
    }
}

void function::dump_halide_stmt() const
{
    if (ENABLE_DEBUG)
    {
        coli::str_dump("\n\n");
        coli::str_dump("\nGenerated Halide Low Level IR:\n");
        std::cout << this->get_halide_stmt();
        coli::str_dump("\n\n\n\n");
    }
}


void function::dump_time_processor_domain() const
{
    // Create time space domain

    if (ENABLE_DEBUG)
    {
        coli::str_dump("\n\nTime-processor domain:\n");

        coli::str_dump("Function " + this->get_name() + ":\n");
        for (const auto &comp : this->get_computations())
        {
            isl_set_dump(comp->get_time_processor_domain());
        }

        coli::str_dump("\n\n");
    }
}

void function::gen_time_processor_domain()
{
    for (auto &comp: this->get_computations())
    {
        comp->gen_time_processor_domain();
    }
}

void computation::dump_schedule() const
{
    if (ENABLE_DEBUG)
    {
        isl_map_dump(this->schedule);
    }
}

void computation::dump() const
{
    if (ENABLE_DEBUG)
    {
        std::cout << "computation \"" << this->name << "\"" << std::endl;
        isl_set_dump(this->get_iteration_domain());
        std::cout << "Schedule " << std::endl;
        isl_map_dump(this->schedule);
        std::cout << "Computation to be scheduled ? " << (this->schedule_this_computation) << std::endl;

        // std::cout << "Computation expression: " << std::endl;
        // this->expression->dump(true);

        for (auto e: this->index_expr)
        {
            coli::str_dump("Access expression:", (const char * ) isl_ast_expr_to_C_str(e));
            coli::str_dump("\n");
        }

        coli::str_dump("Halide statement:\n");
        if (this->stmt.defined())
        {
            std::cout << this->stmt;
        }
        else
        {
            coli::str_dump("NULL");
        }
        coli::str_dump("\n");
    }
}

void computation::set_schedule(std::string map_str)
{
    assert(map_str.length() > 0);
    assert(this->ctx != NULL);

    isl_map *map = isl_map_read_from_str(this->ctx, map_str.c_str());
    map = update_let_stmt_schedule_domain_name(map);
    assert(map != NULL);

    this->set_schedule(map);
}


isl_map *isl_map_set_const_dim(isl_map *map, int dim_pos, int val)
{
    DEBUG_FCT_NAME(3);
    DEBUG_INDENT(4);

    assert(map != NULL);
    assert(dim_pos >= 0);
    assert(dim_pos <= (signed int) isl_map_dim(map, isl_dim_out));

    DEBUG(3, coli::str_dump("Setting the constant coefficient of ",
                            isl_map_to_str(map));
             coli::str_dump(" at dimension ");
             coli::str_dump(std::to_string(dim_pos));
             coli::str_dump(" into ");
             coli::str_dump(std::to_string(val)));

    isl_space *sp = isl_map_get_space(map);
    isl_local_space *lsp =
        isl_local_space_from_space(isl_space_copy(sp));

    isl_map *identity = isl_set_identity(isl_map_range(isl_map_copy(map)));
    identity = isl_map_universe(isl_map_get_space(identity));

    sp = isl_map_get_space(identity);
    lsp = isl_local_space_from_space(isl_space_copy(sp));

    for (int i=0; i<isl_map_dim(identity, isl_dim_out); i++)
        if (i == dim_pos)
        {
            isl_constraint *cst = isl_constraint_alloc_equality(isl_local_space_copy(lsp));
            cst = isl_constraint_set_coefficient_si(cst, isl_dim_out, dim_pos, 1);
            cst = isl_constraint_set_constant_si(cst, (-1)*(val));
            identity = isl_map_add_constraint(identity, cst);
        }
        else
        {
            isl_constraint *cst2 = isl_constraint_alloc_equality(isl_local_space_copy(lsp));
            cst2 = isl_constraint_set_coefficient_si(cst2, isl_dim_in, i, 1);
            cst2 = isl_constraint_set_coefficient_si(cst2, isl_dim_out, i, -1);
            identity = isl_map_add_constraint(identity, cst2);
        }

    DEBUG(3, coli::str_dump("Transformation map ", isl_map_to_str(identity)));

    map = isl_map_apply_range(map, identity);

    DEBUG(3, coli::str_dump("After applying the transformation map: ",
                            isl_map_to_str(map)));

    DEBUG_INDENT(-4);

    return map;
}


/**
  * Add a dimension to the map in the specified position.
  * A constraint that indicates that the dim is equal to a constant
  * is added.
  */
isl_map *isl_map_add_dim_and_eq_constraint(isl_map *map, int dim_pos, int constant)
{
    assert(map != NULL);
    assert(dim_pos >= 0);
    assert(dim_pos <= (signed int) isl_map_dim(map, isl_dim_out));

    map = isl_map_insert_dims(map, isl_dim_out, dim_pos, 1);

    isl_space *sp = isl_map_get_space(map);
    isl_local_space *lsp =
        isl_local_space_from_space(isl_space_copy(sp));
    isl_constraint *cst = isl_constraint_alloc_equality(lsp);
    cst = isl_constraint_set_coefficient_si(cst, isl_dim_out, dim_pos, 1);
    cst = isl_constraint_set_constant_si(cst, (-1)*constant);
    map = isl_map_add_constraint(map, cst);

    return map;
}


void computation::after(computation &comp, int dim)
{
    isl_map *sched1;

    DEBUG_FCT_NAME(3);
    DEBUG_INDENT(4);

    DEBUG(3, coli::str_dump("Setting the schedule of ");
             coli::str_dump(this->get_name());
             coli::str_dump(" after ");
             coli::str_dump(comp.get_name());
             coli::str_dump(" at dimension ");
             coli::str_dump(std::to_string(dim)));

    std::vector<std::pair<int, coli::computation *>> ordered_computations;

    comp.get_function()->align_schedules();

    // Go through all the computations.
    for (auto c: this->get_function()->get_computations())
    {
        sched1 = c->get_schedule();

        DEBUG(3, coli::str_dump("Preparing to adjust the schedule of the computation ");
                 coli::str_dump(c->get_name()));
        DEBUG(3, coli::str_dump("Original schedule: ", isl_map_to_str(sched1)));

        assert(sched1 != NULL);
        DEBUG(3, coli::str_dump("Dimension level after which ordering dimensions will be inserted : ");
                 coli::str_dump(std::to_string(dim)));
        DEBUG(3, coli::str_dump("Original number of dimensions of the schedule : ");
                 coli::str_dump(std::to_string(isl_map_dim(sched1, isl_dim_out))));
        assert(dim < (signed int) isl_map_dim(sched1, isl_dim_out));
        assert(dim >= computation::root_dimension);

        // Update relative orders.
        c->relative_order = c->relative_order*10;
        if ((c != &comp))
            c->relative_order = c->relative_order + 1;

        DEBUG(3, coli::str_dump("Calculated relative order: ");
                 coli::str_dump(std::to_string(c->relative_order)));

        DEBUG(3, coli::str_dump("Adding the computation to the vector of ordered computations."));

        ordered_computations.push_back(std::pair<int, coli::computation *>(c->relative_order, c));
    }

    std::sort(ordered_computations.begin(), ordered_computations.end());

    DEBUG(3, coli::str_dump("Setting the schedules according to their orders."));

    int order = 0;
    for (auto c: ordered_computations)
    {
        sched1 = c.second->get_schedule();

        sched1 = isl_map_set_const_dim(sched1, dim+1, order);
        order++;

        c.second->set_schedule(sched1);
        DEBUG(3, coli::str_dump("Schedule adjusted: ", isl_map_to_str(sched1)));
    }

    DEBUG_INDENT(-4);
}


void computation::first(int dim)
{
    isl_map *sched1;

    DEBUG_FCT_NAME(3);
    DEBUG_INDENT(4);

    this->get_function()->align_schedules();

    // Go through all the computations.
    for (auto c: this->get_function()->get_computations())
    {
        sched1 = c->get_schedule();

        DEBUG(3, coli::str_dump("Adjusting the schedule of a computation ");
                 coli::str_dump(c->get_name()));
        DEBUG(3, coli::str_dump("Original schedule: ", isl_map_to_str(sched1)));

        assert(sched1 != NULL);
        DEBUG(3, coli::str_dump("Dimension level after which the ordering dimension will be inserted : ");
                 coli::str_dump(std::to_string(dim)));
        DEBUG(3, coli::str_dump("Original number of dimensions of the schedule : ");
                 coli::str_dump(std::to_string(isl_map_dim(sched1, isl_dim_out))));
        assert(dim < (signed int) isl_map_dim(sched1, isl_dim_out));
        assert(dim >= computation::root_dimension);

        // Update relative orders.
        if (c != this)
            c->relative_order = c->relative_order + 1;

        DEBUG(3, coli::str_dump("Relative order: ");
                 coli::str_dump(std::to_string(c->relative_order)));

        if (c == this)
        {
            sched1 = isl_map_set_const_dim(sched1, dim+1, 0);
            DEBUG(3, coli::str_dump("Setting dimension to 0."));
        }
        else
        if (c != this)
        {
            sched1 = isl_map_set_const_dim(sched1, dim+1, 1);
            DEBUG(3, coli::str_dump("Setting dimension to 1."));
        }

        c->set_schedule(sched1);
        DEBUG(3, coli::str_dump("Schedule adjusted: ", isl_map_to_str(sched1)));
    }

    DEBUG_INDENT(-4);
}


void computation::before(computation &comp, int dim)
{
    DEBUG_FCT_NAME(3);
    DEBUG_INDENT(4);

    comp.after(*this, dim);

    DEBUG_INDENT(-4);
}

void computation::tile(int inDim0, int inDim1,
            int sizeX, int sizeY)
{
    // Check that the two dimensions are consecutive.
    // Tiling only applies on a consecutive band of loop dimensions.
    assert((inDim0 == inDim1+2) || (inDim1 == inDim0+2));
    assert(sizeX > 0);
    assert(sizeY > 0);
    assert(inDim0 >= 0);
    assert(inDim1 >= 0);
    assert(this->get_iteration_domain() != NULL);
    assert(inDim1 < isl_space_dim(isl_map_get_space(this->schedule), isl_dim_out));

    DEBUG_FCT_NAME(3);
    DEBUG_INDENT(4);

    int new_inDim1 = inDim1 + 2;
    this->split(inDim0, sizeX);
    this->split(new_inDim1, sizeY);
    this->interchange(inDim0+2, new_inDim1);

    DEBUG_INDENT(-4);
}

/**
 * Modify the schedule of this computation so that the two dimensions
 * inDim0 and inDime1 are interchanged (swaped).
 */
void computation::interchange(int inDim0, int inDim1)
{
    assert(inDim0 >= 0);
    assert(inDim0 < isl_space_dim(isl_map_get_space(this->schedule),
                                  isl_dim_out));
    assert(inDim1 >= 0);
    assert(inDim1 < isl_space_dim(isl_map_get_space(this->schedule),
                                  isl_dim_out));

    DEBUG_FCT_NAME(3);
    DEBUG_INDENT(4);

    isl_map *schedule = this->get_schedule();

    DEBUG(3, coli::str_dump("Original schedule: ", isl_map_to_str(schedule)));

    int n_dims = isl_map_dim(schedule, isl_dim_out);

    std::string inDim0_str = isl_map_get_dim_name(schedule, isl_dim_out, inDim0);
    std::string inDim1_str = isl_map_get_dim_name(schedule, isl_dim_out, inDim1);

    std::vector<isl_id *> dimensions;

    std::string map = "{[";

    for (int i=0; i<n_dims; i++)
    {
        if (isl_map_get_dim_name(schedule, isl_dim_out, i) == NULL)
        {
            isl_id *new_id = isl_id_alloc(this->get_ctx(), generate_new_variable_name().c_str(), NULL);
            schedule = isl_map_set_dim_id(schedule, isl_dim_out, i,new_id);
        }
        map = map + isl_map_get_dim_name(schedule, isl_dim_out, i);
        if (i != n_dims-1)
            map = map + ",";
    }

    map = map + "] -> [";

    for (int i=0; i<n_dims; i++)
    {
        if ((i != inDim0) && (i != inDim1))
        {
            map = map + isl_map_get_dim_name(schedule, isl_dim_out, i);
            dimensions.push_back(isl_map_get_dim_id(schedule, isl_dim_out, i));
        }
        else if (i == inDim0)
        {
            map = map + inDim1_str;
            isl_id *id1 = isl_id_alloc(this->get_ctx(), inDim1_str.c_str(), NULL);
            dimensions.push_back(id1);
        }
        else if (i == inDim1)
        {
            map = map + inDim0_str;
            isl_id *id1 = isl_id_alloc(this->get_ctx(), inDim0_str.c_str(), NULL);
            dimensions.push_back(id1);
        }

        if (i != n_dims-1)
            map = map + ",";
    }

    map = map + "]}";

    DEBUG(3, coli::str_dump("Transformation map = ", map.c_str()));

    isl_map *transformation_map = isl_map_read_from_str(this->get_ctx(), map.c_str());
    transformation_map = isl_map_set_tuple_id(
        transformation_map, isl_dim_in, isl_map_get_tuple_id(isl_map_copy(schedule), isl_dim_out));
    isl_id *id_range = isl_id_alloc(this->get_ctx(), "", NULL);
    transformation_map = isl_map_set_tuple_id(
        transformation_map, isl_dim_out, id_range);
    schedule = isl_map_apply_range(isl_map_copy(schedule), isl_map_copy(transformation_map));

    DEBUG(3, coli::str_dump("Schedule after interchange: ", isl_map_to_str(schedule)));

    this->set_schedule(schedule);

    DEBUG_INDENT(-4);
}

/**
 * Modify the schedule of this computation so that it splits the
 * dimension inDim0 of the iteration space into two new dimensions.
 * The size of the inner dimension created is sizeX.
 */
void computation::split(int inDim0, int sizeX)
{
    assert(this->get_schedule() != NULL);
    assert(inDim0 >= 0);
    assert(inDim0 < isl_space_dim(isl_map_get_space(this->get_schedule()), isl_dim_out));
    assert(sizeX >= 1);

    DEBUG_FCT_NAME(3);
    DEBUG_INDENT(4);

    isl_map *schedule = this->get_schedule();
    schedule = isl_map_copy(schedule);
    schedule = isl_map_set_tuple_id(schedule, isl_dim_out,
                                    isl_id_alloc(this->get_ctx(), " ", NULL));


    DEBUG(3, coli::str_dump("Original schedule: ", isl_map_to_str(schedule)));
    DEBUG(3, coli::str_dump("Splitting dimension " + std::to_string(inDim0)
                            + " with split size " + std::to_string(sizeX)));

    std::string inDim0_str;

    std::string outDim0_str = generate_new_variable_name();
    std::string outDim1_str = generate_new_variable_name();

    int n_dims = isl_map_dim(this->get_schedule(), isl_dim_out);
    std::string map = "{[";

    std::vector<isl_id *> dimensions;
    std::vector<std::string> dimensions_str;

    for (int i=0; i<n_dims; i++)
    {
        std::string dim_str = generate_new_variable_name();
        dimensions_str.push_back(dim_str);
        map = map + dim_str;

        if (i == inDim0)
            inDim0_str = dim_str;

        if (i != n_dims-1)
            map = map + ",";
    }

    map = map + "] -> [";

    for (int i=0; i<n_dims; i++)
    {
        if (i != inDim0)
        {
            map = map + dimensions_str[i];
            dimensions.push_back(isl_id_alloc(
                                    this->get_ctx(),
                                    dimensions_str[i].c_str(),
                                    NULL));
        }
        else
        {
            map = map + outDim0_str + ", 0, " + outDim1_str;
            isl_id *id0 = isl_id_alloc(this->get_ctx(),
                                       outDim0_str.c_str(), NULL);
            isl_id *id1 = isl_id_alloc(this->get_ctx(),
                                       outDim1_str.c_str(), NULL);
            dimensions.push_back(id0);
            dimensions.push_back(id1);
        }

        if (i != n_dims-1)
            map = map + ",";
    }

    map = map + "] : " + outDim0_str + " = floor(" + inDim0_str + "/" +
        std::to_string(sizeX) + ") and " + outDim1_str + " = (" +
        inDim0_str + "%" + std::to_string(sizeX) + ")}";

    DEBUG(3, coli::str_dump("Transformation map = ", map.c_str()));

    isl_map *transformation_map = isl_map_read_from_str(this->get_ctx(), map.c_str());

    for (int i=0; i< dimensions.size(); i++)
        transformation_map = isl_map_set_dim_id(
            transformation_map, isl_dim_out, i, isl_id_copy(dimensions[i]));

    transformation_map = isl_map_set_tuple_id(
        transformation_map, isl_dim_in,
        isl_map_get_tuple_id(isl_map_copy(schedule), isl_dim_out));
    isl_id *id_range = isl_id_alloc(this->get_ctx(), " ", NULL);
    transformation_map = isl_map_set_tuple_id(transformation_map, isl_dim_out, id_range);
    schedule = isl_map_apply_range(isl_map_copy(schedule), isl_map_copy(transformation_map));

    DEBUG(3, coli::str_dump("Schedule after splitting: ", isl_map_to_str(schedule)));

    this->set_schedule(schedule);

    DEBUG_INDENT(-4);
}

// Methods related to the coli::function class.

std::string coli::function::get_gpu_iterator(std::string comp, int lev0) const
{
   assert(comp.length() > 0);
   assert(lev0 >=0 );

   DEBUG_FCT_NAME(3);
   DEBUG_INDENT(4);

   std::string res = std::string("");;

   const auto &levels = this->gpu_dimensions.find(comp);
   assert(levels != this->gpu_dimensions.end());

   if (lev0 == levels->second.first)
     res = std::string("__thread_id_x");
   else if (lev0 == levels->second.second)
     res = std::string("__thread_id_y");
   else
     coli::error("Level not mapped to GPU.", true);

   std::string str = std::string("Dimension ") + std::to_string(lev0)
       + std::string(" should be mapped to iterator ") + res;
   str = str + ". It was compared against: " + std::to_string(levels->second.first)
         + " and " + std::to_string(levels->second.second);
   DEBUG(3, coli::str_dump(str));

   DEBUG_INDENT(-4);
   return res;
}

bool coli::function::should_map_to_gpu(std::string comp, int lev0) const
{
      assert(comp.length() > 0);
      assert(lev0 >=0 );

      DEBUG_FCT_NAME(10);
      DEBUG_INDENT(4);

      bool res;

      const auto &levels = this->gpu_dimensions.find(comp);
      if (levels == this->gpu_dimensions.end())
          res = false;
      else
          res = ((lev0 == levels->second.first) || (lev0 == levels->second.second));

      std::string str = std::string("Dimension ") + std::to_string(lev0)
          + std::string(res?" should":" should not")
          + std::string(" be mapped to GPU.");
      DEBUG(10, coli::str_dump(str));

      DEBUG_INDENT(-4);
      return res;
}

int coli::function::get_max_schedules_range_dim() const
{
    int max_dim = 0;

    for (const auto &comp : this->get_computations())
    {
        int m = isl_map_dim(comp->get_schedule(), isl_dim_out);
        max_dim = std::max(max_dim, m);
    }

    return max_dim;
}

isl_map *isl_map_align_range_dims(isl_map *map, int max_dim)
{
    DEBUG_FCT_NAME(10);
    DEBUG_INDENT(4);

    assert(map != NULL);
    int mdim = isl_map_dim(map, isl_dim_out);
    assert(max_dim >= mdim);

    DEBUG(10, coli::str_dump("Debugging isl_map_align_range_dims()."));
    DEBUG(10, coli::str_dump("Input map:", isl_map_to_str(map)));

    map = isl_map_add_dims(map, isl_dim_out, max_dim - mdim);

    for (int i=mdim; i<max_dim; i++)
    {
        isl_space *sp = isl_map_get_space(map);
        isl_local_space *lsp =
            isl_local_space_from_space(isl_space_copy(sp));
        isl_constraint *cst = isl_constraint_alloc_equality(lsp);
        cst = isl_constraint_set_coefficient_si(cst, isl_dim_out, i, 1);
        map = isl_map_add_constraint(map, cst);
    }

    DEBUG(10, coli::str_dump("After alignment, map = ",
                isl_map_to_str(map)));

    DEBUG_INDENT(-4);
    return map;
}

void coli::function::align_schedules()
{
    DEBUG_FCT_NAME(3);
    DEBUG_INDENT(4);

    int max_dim = this->get_max_schedules_range_dim();

    for (auto &comp : this->get_computations())
    {
        isl_map *sched = comp->get_schedule();
        assert((sched != NULL) && "Schedules should be set before calling align_schedules");
        sched = isl_map_align_range_dims(sched, max_dim);
        comp->set_schedule(sched);
    }

    DEBUG_INDENT(-4);
    DEBUG(3, coli::str_dump("End of function"));
}

void coli::function::add_invariant(coli::constant invar)
{
    invariants.push_back(invar);
}

void coli::function::add_computation(computation *cpt)
{
    assert(cpt != NULL);

    assert(std::find_if(this->body.begin(), this->body.end(),
                        [&cpt](const computation *c) { return (c->get_name() == cpt->get_name()); }) ==
           this->body.end() &&
           "Found duplicate of cpt.");

    this->body.push_back(cpt);
}

void coli::constant::dump(bool exhaustive) const
{
    if (ENABLE_DEBUG)
    {
        std::cout << "Invariant \"" << this->name << "\"" << std::endl;

        std::cout << "Expression: ";
        this->get_expr()->dump(exhaustive);
        std::cout << std::endl;
    }
}

void coli::function::dump(bool exhaustive) const
{
    if (ENABLE_DEBUG)
    {
        std::cout << "\n\nFunction \"" << this->name << "\"" << std::endl;

        std::cout << "Function arguments (coli buffers):" << std::endl;
        for (const auto &buf : this->function_arguments)
        {
            buf->dump(exhaustive);
        }
        std::cout << std::endl;

        std::cout << "Function invariants:" << std::endl;
        for (const auto &inv : this->invariants)
        {
            inv.dump(exhaustive);
        }
        std::cout << std::endl;

        std::cout << "Function context set:";
        isl_set_dump(this->get_context_set());
        std::cout << std::endl;

        this->dump_schedule();

        std::cout << "Body " << std::endl;
        for (const auto &cpt : this->body)
            cpt->dump();

        std::cout<< std::endl;

        if (this->halide_stmt != NULL)
        {
            std::cout << "Halide stmt " << *(this->halide_stmt) << std::endl;
        }

        std::cout << "Buffers" << std::endl;
        for (const auto &buf : this->buffers_list)
        {
            std::cout << "Buffer name: " << buf.second->get_name() << std::endl;
        }

        std::cout << std::endl << std::endl;
    }
}

void coli::function::dump_iteration_domain() const
{
    if (ENABLE_DEBUG)
    {
        coli::str_dump("\nIteration domain:\n");
        for (const auto &cpt : this->body)
            cpt->dump_iteration_domain();
        coli::str_dump("\n");
    }
}

void coli::function::dump_schedule() const
{
    if (ENABLE_DEBUG)
    {
        coli::str_dump("\nSchedule:\n");

        for (const auto &cpt : this->body)
        {
            cpt->dump_schedule();
        }

        std::cout << "Parallel dimensions: ";
        for (const auto &par_dim : parallel_dimensions)
        {
            std::cout << par_dim.first << "(" << par_dim.second << ") ";
        }

        std::cout << std::endl;

        std::cout << "Vector dimensions: ";
        for (const auto &vec_dim : vector_dimensions)
        {
            std::cout << vec_dim.first << "(" << vec_dim.second << ") ";
        }

        std::cout<< std::endl << std::endl << std::endl;
    }
}

Halide::Argument::Kind halide_argtype_from_coli_argtype(coli::argument_t type)
{
    Halide::Argument::Kind res;

    if (type == coli::a_temporary)
        coli::error("Buffer type \"temporary\" can't be translated to Halide.\n", true);

    if (type == coli::a_input)
    {
        res = Halide::Argument::InputBuffer;
    }
    else
    {
        res = Halide::Argument::OutputBuffer;
    }

    return res;
}

void coli::function::set_arguments(std::vector<coli::buffer *> buffer_vec)
{
    this->function_arguments = buffer_vec;
}

void coli::function::add_vector_dimension(std::string stmt_name, int vec_dim)
{
    assert(vec_dim >= 0);
    assert(stmt_name.length() > 0);

    this->vector_dimensions.insert(std::pair<std::string,int>(stmt_name, vec_dim));
}

void coli::function::add_parallel_dimension(std::string stmt_name, int vec_dim)
{
    assert(vec_dim >= 0);
    assert(stmt_name.length() > 0);

    this->parallel_dimensions.insert(std::pair<std::string,int>(stmt_name, vec_dim));
}

void coli::function::add_gpu_dimensions(std::string stmt_name, int dim0,
                                        int dim1)
{
    assert(dim0 >= 0);
    assert(dim1 >= 0);
    assert(dim1 == dim0 + 1);
    assert(stmt_name.length() > 0);

    this->gpu_dimensions.insert(std::pair<std::string, std::pair<int,int>>
                                          (stmt_name,  std::pair<int,int>
                                                         (dim0, dim1)));
}

isl_union_set * coli::function::get_time_processor_domain()
{
    isl_union_set *result = NULL;
    isl_space *space = NULL;

    if (this->body.empty() == false)
    {
        space = isl_set_get_space(this->body[0]->get_iteration_domain());
    }
    else
    {
        return NULL;
    }

    assert(space != NULL);
    result = isl_union_set_empty(isl_space_copy(space));

    for (const auto &cpt : this->body)
    {
        isl_set *cpt_iter_space = isl_set_copy(cpt->get_time_processor_domain());
        result = isl_union_set_union(isl_union_set_from_set(cpt_iter_space), result);
    }

    return result;
}


isl_union_set *coli::function::get_iteration_domain() const
{
    isl_union_set *result = NULL;
    isl_space *space = NULL;

    if (this->body.empty() == false)
    {
        space = isl_set_get_space(this->body[0]->get_iteration_domain());
    }
    else
    {
        return NULL;
    }

    assert(space != NULL);
    result = isl_union_set_empty(isl_space_copy(space));

    for (const auto &cpt : this->body)
    {
        if (cpt->should_schedule_this_computation())
        {
            isl_set *cpt_iter_space = isl_set_copy(cpt->get_iteration_domain());
            result = isl_union_set_union(isl_union_set_from_set(cpt_iter_space), result);
        }
    }

    return result;
}

isl_union_map *coli::function::get_schedule() const
{
    isl_union_map *result = NULL;
    isl_space *space = NULL;

    if (this->body.empty() == false)
    {
        space = isl_map_get_space(this->body[0]->get_schedule());
    }
    else
    {
        return NULL;
    }

    assert(space != NULL);
    result = isl_union_map_empty(isl_space_copy(space));

    for (const auto &cpt : this->body)
    {
        isl_map *m = isl_map_copy(cpt->get_schedule());
        result = isl_union_map_union(isl_union_map_from_map(m), result);
    }

    result = isl_union_map_intersect_domain(result, this->get_iteration_domain());

    return result;
}

// Function for the buffer class

std::string str_coli_type_op(coli::op_t type)
{
    switch (type)
    {
        case coli::o_logical_and:
            return "and";
        case coli::o_logical_or:
            return "or";
        case coli::o_max:
            return "max";
        case coli::o_min:
            return "min";
        case coli::o_minus:
            return "mins";
        case coli::o_add:
            return "add";
        case coli::o_sub:
            return "sub";
        case coli::o_mul:
            return "mul";
        case coli::o_div:
            return "div";
        case coli::o_mod:
            return "mod";
        case coli::o_cond:
            return "cond";
        case coli::o_not:
            return "not";
        case coli::o_eq:
            return "eq";
        case coli::o_ne:
            return "ne";
        case coli::o_le:
            return "le";
        case coli::o_lt:
            return "lt";
        case coli::o_ge:
            return "ge";
        case coli::o_call:
            return "call";
        case coli::o_access:
            return "access";
        default:
            coli::error("coli op not supported.", true);
            return "";
    }
}

std::string str_from_coli_type_expr(coli::expr_t type)
{
    switch (type)
    {
        case coli::e_id:
            return "id";
        case coli::e_val:
            return "val";
        case coli::e_op:
            return "op";
        default:
            coli::error("Coli type not supported.", true);
            return "";
    }
}

std::string str_from_coli_type_argument(coli::argument_t type)
{
    switch (type)
    {
        case coli::a_input:
            return "input";
        case coli::a_output:
            return "output";
        case coli::a_temporary:
            return "temporary";
        default:
            coli::error("Coli type not supported.", true);
            return "";
    }
}

std::string str_from_coli_type_primitive(coli::primitive_t type)
{
    switch (type)
    {
        case coli::p_uint8:
            return "uint8";
        case coli::p_int8:
            return "int8";
        case coli::p_uint16:
            return "uint16";
        case coli::p_int16:
            return "int16";
        case coli::p_uint32:
            return "uin32";
        case coli::p_int32:
            return "int32";
        case coli::p_uint64:
            return "uint64";
        case coli::p_int64:
            return "int64";
        case coli::p_float32:
            return "float32";
        case coli::p_float64:
            return "float64";
        case coli::p_boolean:
            return "bool";
        default:
            coli::error("Coli type not supported.", true);
            return "";
    }
}

std::string str_from_is_null(void *ptr)
{
    return ((ptr != NULL) ? "Not NULL" : "NULL");
}

void coli::buffer::dump(bool exhaustive) const
{
    if (ENABLE_DEBUG)
    {
        std::cout << "Buffer \"" << this->name
                  << "\", Number of dimensions: " << this->nb_dims
                  << std::endl;

        std::cout << "Dimension sizes: ";
        for (auto size: dim_sizes)
        {
          // TODO: create_halide_expr_from_coli_expr does not support
          // the case where the buffer size is a computation access.
            std::vector<isl_ast_expr *> ie = {};
            std::cout << halide_expr_from_coli_expr(NULL, ie, size) << ", ";
        }

        std::cout << std::endl;

        std::cout << "Elements type: "
                  << str_from_coli_type_primitive(this->type) << std::endl;

        std::cout << "Data field: "
                  << str_from_is_null(this->data) << std::endl;

        std::cout << "Function field: "
                  << str_from_is_null(this->fct) << std::endl;

        std::cout << "Argument type: "
                  << str_from_coli_type_argument(this->argtype) << std::endl;

        std::cout<< std::endl << std::endl;
    }
}

Halide::Type halide_type_from_coli_type(coli::primitive_t type)
{
    Halide::Type t;

    switch (type)
    {
        case coli::p_uint8:
            t = Halide::UInt(8);
            break;
        case coli::p_int8:
            t = Halide::Int(8);
            break;
        case coli::p_uint16:
            t = Halide::UInt(16);
            break;
        case coli::p_int16:
            t = Halide::Int(16);
            break;
        case coli::p_uint32:
            t = Halide::UInt(32);
            break;
        case coli::p_int32:
            t = Halide::Int(32);
            break;
        case coli::p_uint64:
            t = Halide::UInt(64);
            break;
        case coli::p_int64:
            t = Halide::Int(64);
            break;
        case coli::p_float32:
            t = Halide::Float(32);
            break;
        case coli::p_float64:
            t = Halide::Float(64);
            break;
        case coli::p_boolean:
            t = Halide::Bool();
            break;
        default:
            coli::error("Coli type cannot be translated to Halide type.", true);
    }
    return t;
}

isl_map* coli::computation::update_let_stmt_schedule_domain_name(isl_map* map)
{
    DEBUG_FCT_NAME(10);
    DEBUG_INDENT(4);

    DEBUG(10, coli::str_dump ("Updating the domain of schedule."));
    DEBUG(10, coli::str_dump ("Input schedule: ", isl_map_to_str(map)));

    // Get the computation,
    // Check if the computation is a let stmt, if it is the case
    // check if it starts with LET_STMT_PREFIX, if not add it automatically.
    std::string comp_name = isl_map_get_tuple_name(map, isl_dim_in);
    assert(comp_name.size() > 0);

    coli::computation* let_comp =
            this->get_function()->get_computation_by_name(comp_name);

    if (let_comp == NULL)    // i.e. if let computation not found
    {
        DEBUG(10, coli::str_dump ("Computation used in the domain not found."));

        // Find the LET_STMT_PREFIX in the name,
        int pos = comp_name.find(LET_STMT_PREFIX);

        // if LET_STMT_PREFIX is not found or is not in the beginning
        // of comp_name, add the prefix.
        if ((pos == std::string::npos) || (pos != 0))
        {
            DEBUG(10, coli::str_dump ("Computation does not have LET_STMT_PREFIX."));
            comp_name = LET_STMT_PREFIX + comp_name;

            // Does adding LET_STMT_PREFIX allow finding the stmt ?
            if (this->get_function()->get_computation_by_name(comp_name) != NULL)
            {
                DEBUG(10, coli::str_dump ("Replacing computation domain."));
                map = isl_map_set_tuple_name(map, isl_dim_in,
                                             comp_name.c_str ());
            }
            else
                coli::error("Scheduling an undeclared computation.", true);
        }
    }

    DEBUG(10, coli::str_dump ("Output schedule: ", isl_map_to_str(map)));
    DEBUG_INDENT(-4);

    return map;
}

}
