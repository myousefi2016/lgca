/*
 * This file is part of LGCA, an implementation of a Lattice Gas Cellular Automaton
 * (https://github.com/keva92/lgca).
 *
 * Copyright (c) 2015-2017 Kerstin Vater, Niklas Kühl, Christian F. Janßen.
 *
 * LGCA is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * LGCA is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with lgca. If not, see <http://www.gnu.org/licenses/>.
 */

#include "lgca_common.h"

#include "omp_lattice.h"

#include <omp.h>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

namespace lgca {

// Definitions of static members
constexpr char          ModelDescriptor<Model::HPP>::INV_DIR[];
constexpr char          ModelDescriptor<Model::HPP>::MIR_DIR_X[];
constexpr char          ModelDescriptor<Model::HPP>::MIR_DIR_Y[];
constexpr Real          ModelDescriptor<Model::HPP>::LATTICE_VEC_X[];
constexpr Real          ModelDescriptor<Model::HPP>::LATTICE_VEC_Y[];
constexpr unsigned char ModelDescriptor<Model::HPP>::COLLISION_LUT[];
constexpr unsigned char ModelDescriptor<Model::HPP>::BB_LUT[];
constexpr unsigned char ModelDescriptor<Model::HPP>::BF_X_LUT[];
constexpr unsigned char ModelDescriptor<Model::HPP>::BF_Y_LUT[];

constexpr char          ModelDescriptor<Model::FHP_I>::INV_DIR[];
constexpr char          ModelDescriptor<Model::FHP_I>::MIR_DIR_X[];
constexpr char          ModelDescriptor<Model::FHP_I>::MIR_DIR_Y[];
constexpr Real          ModelDescriptor<Model::FHP_I>::LATTICE_VEC_X[];
constexpr Real          ModelDescriptor<Model::FHP_I>::LATTICE_VEC_Y[];
constexpr unsigned char ModelDescriptor<Model::FHP_I>::COLLISION_LUT[];
constexpr unsigned char ModelDescriptor<Model::FHP_I>::BB_LUT[];
constexpr unsigned char ModelDescriptor<Model::FHP_I>::BF_X_LUT[];
constexpr unsigned char ModelDescriptor<Model::FHP_I>::BF_Y_LUT[];

constexpr char          ModelDescriptor<Model::FHP_II>::INV_DIR[];
constexpr char          ModelDescriptor<Model::FHP_II>::MIR_DIR_X[];
constexpr char          ModelDescriptor<Model::FHP_II>::MIR_DIR_Y[];
constexpr Real          ModelDescriptor<Model::FHP_II>::LATTICE_VEC_X[];
constexpr Real          ModelDescriptor<Model::FHP_II>::LATTICE_VEC_Y[];
constexpr unsigned char ModelDescriptor<Model::FHP_II>::COLLISION_LUT[];
constexpr unsigned char ModelDescriptor<Model::FHP_II>::BB_LUT[];
constexpr unsigned char ModelDescriptor<Model::FHP_II>::BF_X_LUT[];
constexpr unsigned char ModelDescriptor<Model::FHP_II>::BF_Y_LUT[];

constexpr char          ModelDescriptor<Model::FHP_III>::INV_DIR[];
constexpr char          ModelDescriptor<Model::FHP_III>::MIR_DIR_X[];
constexpr char          ModelDescriptor<Model::FHP_III>::MIR_DIR_Y[];
constexpr Real          ModelDescriptor<Model::FHP_III>::LATTICE_VEC_X[];
constexpr Real          ModelDescriptor<Model::FHP_III>::LATTICE_VEC_Y[];
constexpr unsigned char ModelDescriptor<Model::FHP_III>::COLLISION_LUT[];
constexpr unsigned char ModelDescriptor<Model::FHP_III>::BB_LUT[];
constexpr unsigned char ModelDescriptor<Model::FHP_III>::BF_X_LUT[];
constexpr unsigned char ModelDescriptor<Model::FHP_III>::BF_Y_LUT[];


// Creates a CUDA parallelized lattice gas cellular automaton object
// of the specified properties.
template<Model model_>
OMP_Lattice<model_>::OMP_Lattice(const string test_case,
                                 const Real Re, const Real Ma_s,
                                 const int coarse_graining_radius)
               : Lattice<model_>(test_case, Re, Ma_s, coarse_graining_radius) {

    // Allocate the memory for the arrays on the host (CPU)
    allocate_memory();

    // Generate random bits for collision
    this->m_rnd_cpu.fill_random();

    // Set the model-based values according to the number of lattice directions
    m_model = new ModelDesc(this->m_dim_x, this->m_dim_y);
}

// Deletes the openMP parallelized lattice gas cellular automaton object.
template<Model model_>
OMP_Lattice<model_>::~OMP_Lattice() {

    delete m_model;

    this->free_memory();
}

// Performs the collision and propagation step on the lattice gas automaton.
template<Model model_>
void OMP_Lattice<model_>::collide_and_propagate(const bool p) {

#ifndef NDEBUG
            // Check weather the domain dimensions are valid for the FHP model.
            if (this->m_dim_y % 2 != 0 && (model_ == Model::FHP_I || model_ == Model::FHP_II || model_ == Model::FHP_III)) {

                printf("ERROR in OMP_Lattice<Model::FHP>::collide_and_propagate(): "
                       "Invalid domain dimension in y direction.\n");
                abort();
            }
#endif

    // Loop over bunches of cells
    const size_t num_blocks = ((this->m_num_cells - 1) / Bitset::BITS_PER_BLOCK) + 1;
    tbb::parallel_for(tbb::blocked_range<size_t>(0, num_blocks), [&](const tbb::blocked_range<size_t>& r) {
    for (size_t block = r.begin(); block != r.end(); ++block)
    {
        for (size_t cell = block * Bitset::BITS_PER_BLOCK; cell < (block+1) * Bitset::BITS_PER_BLOCK; ++cell) {

            if (cell >= this->m_num_cells) break;

            // Calculate the position of the cell in y direction (row index)
            int pos_y = cell / this->m_dim_x;

            // Get the type of the cell, i.e. fluid or solid
            // This has to be taken into account during the collision step, where cells behave
            // different according to their type
            CellType cell_type = this->m_cell_type_cpu[cell];

            // Check weather the cell is located on boundaries
            bool on_eastern_boundary  = (cell + 1) % this->m_dim_x == 0;
            bool on_northern_boundary = cell >= (this->m_num_cells - this->m_dim_x);
            bool on_western_boundary  = cell % this->m_dim_x == 0;
            bool on_southern_boundary = cell < this->m_dim_x;

            // Define an array for the states of the nodes in the cell
            unsigned char node_state[this->NUM_DIR];
//            Bitset node_state(this->NUM_DIR);

            // Execute propagation step
#pragma unroll
            for (int dir = 0; dir < this->NUM_DIR; dir++)
            {
                int inv_dir = ModelDesc::INV_DIR[dir];

                // Reset the memory offset
                int offset = 0;

                // The cell is located in a row with even index value
                if (pos_y % 2 == 0)
                {
                    // Construct the correct memory offset
                    //
                    // Apply a default offset value
                    offset += m_model->offset_to_neighbor_even[inv_dir];

                    // Correct the offset in the current direction if the cell is located on boundaries
                    if (on_eastern_boundary)  offset += m_model->offset_to_western_boundary_even [inv_dir];
                    if (on_northern_boundary) offset += m_model->offset_to_southern_boundary_even[inv_dir];
                    if (on_western_boundary)  offset += m_model->offset_to_eastern_boundary_even [inv_dir];
                    if (on_southern_boundary) offset += m_model->offset_to_northern_boundary_even[inv_dir];

                // The cell is located in a row with odd index value
                } else if (pos_y % 2 != 0) {

                    // Construct the correct memory offset
                    //
                    // Apply a default offset value
                    offset += m_model->offset_to_neighbor_odd[inv_dir];

                    // Correct the offset in the current direction if the cell is located on boundaries
                    if (on_eastern_boundary)  offset += m_model->offset_to_western_boundary_odd [inv_dir];
                    if (on_northern_boundary) offset += m_model->offset_to_southern_boundary_odd[inv_dir];
                    if (on_western_boundary)  offset += m_model->offset_to_eastern_boundary_odd [inv_dir];
                    if (on_southern_boundary) offset += m_model->offset_to_northern_boundary_odd[inv_dir];
                }

                // Pull the states of the cell from its "neighbor" cells in the different directions
                node_state[dir] = bool(this->m_node_state_cpu[dir + (cell + offset) * 8]);
            }

            // Execute collision step
            //
            // Create a temporary array to copy the node states
            unsigned char node_state_tmp[this->NUM_DIR];
//            Bitset node_state_tmp(this->NUM_DIR);

            // Copy the actual states of the nodes to the temporary array
#pragma unroll
            for (int dir = 0; dir < this->NUM_DIR; ++dir) node_state_tmp[dir] = node_state[dir];
//            node_state_tmp(0) = node_state(0);

            switch (cell_type) {

            // The cell working on is a fluid cell ("normal" collision)
            case CellType::FLUID:
            {
                ModelDesc::collide(&node_state[0], &node_state_tmp[0], bool(this->m_rnd_cpu[cell]));
//                ModelDesc::collide(&node_state(0), &node_state_tmp(0), bool(this->m_rnd_cpu[cell]));
                break;
            }

            // The cell working on is a solid cell of bounce back type
            case CellType::SOLID_NO_SLIP:
            {
                ModelDesc::bounce_back(&node_state[0], &node_state_tmp[0]);
//                ModelDesc::bounce_back(&node_state(0), &node_state_tmp(0));
                break;
            }

            // The cell working on is a solid cell of bounce forward type
            case CellType::SOLID_SLIP:
            {
                if (on_northern_boundary || on_southern_boundary) {

                    // Exchange the states of the nodes with the the states of the mirrored
                    // directions along the x axis
                    ModelDesc::bounce_forward_x(&node_state[0], &node_state_tmp[0]);
//                    ModelDesc::bounce_forward_x(&node_state(0), &node_state_tmp(0));
                }

                if (on_eastern_boundary || on_western_boundary) {

                    // Exchange the states of the nodes with the the states of
                    // the mirrored directions along the y axis
                    ModelDesc::bounce_forward_y(&node_state[0], &node_state_tmp[0]);
//                    ModelDesc::bounce_forward_y(&node_state(0), &node_state_tmp(0));
                }
                break;
            }
            }

            // Write new node states back to global array
#pragma unroll
            for (int dir = 0; dir < this->NUM_DIR; dir++)
            {
                this->m_node_state_tmp_cpu[dir + cell * 8] = bool(node_state_tmp[dir]);
            }
//            this->m_node_state_tmp_cpu(cell) = node_state_tmp(0);

        } /* FOR cell */

    }}); /* FOR block */

    // Update the node states
    auto node_state_cpu_tmp = this->m_node_state_cpu.ptr();
    this->m_node_state_cpu  = m_node_state_tmp_cpu.ptr();
    m_node_state_tmp_cpu    = node_state_cpu_tmp;
}

// Applies a body force in the specified direction (x or y) and with the
// specified intensity to the particles. E.g., if the intensity is equal 100,
// every 100th particle changes it's direction, if feasible.
template<Model model_>
void OMP_Lattice<model_>::apply_body_force(const int forcing) {

    // Set a maximum number of iterations to find particles which can be reverted
    const size_t it_max = 2 * this->m_num_cells;

    // Set the number of iterations to zero
    size_t it = 0;

    // Number of particles which have been reverted
    unsigned int reverted_particles = 0;

    // Loop over all cells
    do
    {
        size_t cell = rand() % this->m_num_cells;

    	it++;

        // Get the type of the cell, i.e. fluid or solid.
        // Note that body forces are applied to fluid cells only.
        CellType cell_type = this->m_cell_type_cpu[cell];

        // Check weather the cell working on is a fluid cell
        if (cell_type == CellType::FLUID) {

            // Define an array for the states of the nodes in the cell
            Bitset node_state(this->NUM_DIR);

            // The thread working on the cell has to know about the states of the nodes within the
            // cell, therefore looping over all directions and look it up
            node_state(0) = this->m_node_state_cpu(cell);

            // Create a temporary array to copy the node states
            Bitset node_state_tmp(this->NUM_DIR);

            // Copy the current states of the nodes to the temporary array
            node_state_tmp(0) = node_state(0);

            if (model_ == Model::HPP) {

                if (this->m_bf_dir == 'x' && (node_state[0] == 0) && (node_state[2] == 1)) {

					node_state_tmp[0] = 1;
					node_state_tmp[2] = 0;

					reverted_particles++;

                } else if (this->m_bf_dir == 'y' && (node_state[1] == 1) && (node_state[3] == 0)) {

					node_state_tmp[1] = 0;
					node_state_tmp[3] = 1;

					reverted_particles++;
				}
        	}

            else if (model_ == Model::FHP_I || model_ == Model::FHP_II || model_ == Model::FHP_III) {

                if (this->m_bf_dir == 'x' && (node_state[0] == 0) && (node_state[3] == 1)) {

					node_state_tmp[0] = 1;
					node_state_tmp[3] = 0;

					reverted_particles++;

                } else if (this->m_bf_dir == 'y') {

					if ((node_state[1] == 1) && (node_state[5] == 0)) {

						node_state_tmp[1] = 0;
						node_state_tmp[5] = 1;

						reverted_particles++;
					}

					if ((node_state[2] == 1) && (node_state[4] == 0)) {

						node_state_tmp[2] = 0;
						node_state_tmp[4] = 1;

						reverted_particles++;
					}
				}
        	}

            // Write the new node states back to the data array
            this->m_node_state_cpu(cell) = node_state_tmp(0);

        } /* IF cell_type */

    } while ((reverted_particles < forcing) && (it < it_max));
}

// Computes quantities of interest as a post-processing procedure
template<Model model_>
void OMP_Lattice<model_>::post_process() {

    // Computes cell quantities of interest as a post-processing procedure
	cell_post_process();

    // Computes coarse grained quantities of interest as a post-processing procedure
	mean_post_process();
}

// Computes cell quantities of interest as a post-processing procedure
template<Model model_>
void OMP_Lattice<model_>::cell_post_process()
{
    // Loop over lattice cells
    tbb::parallel_for(tbb::blocked_range<size_t>(0, this->m_num_cells), [&](const tbb::blocked_range<size_t>& r) {
    for (size_t cell = r.begin(); cell != r.end(); ++cell)
    {
        // Initialize the cell quantities to be computed
        char cell_density    = 0;
        Real cell_momentum_x = 0.0;
        Real cell_momentum_y = 0.0;

        // Loop over nodes within the current cell
#pragma unroll
        for (int dir = 0; dir < this->NUM_DIR; ++dir) {

            char node_state = bool(this->m_node_state_out_cpu[dir + cell * 8]);

            // Sum up the node states
            cell_density += node_state;

            // Sum up the node states multiplied by the lattice vector component for the current
            // direction
            cell_momentum_x += node_state * ModelDesc::LATTICE_VEC_X[dir];
            cell_momentum_y += node_state * ModelDesc::LATTICE_VEC_Y[dir];
        }

        // Write the computed cell quantities to the related data arrays
        this->m_cell_density_cpu [cell                        ] = (Real) cell_density;
        this->m_cell_momentum_cpu[cell * this->SPATIAL_DIM    ] = cell_momentum_x;
        this->m_cell_momentum_cpu[cell * this->SPATIAL_DIM + 1] = cell_momentum_y;

    } // for cell
    });
}

// Computes coarse grained quantities of interest as a post-processing procedure
template<Model model_>
void OMP_Lattice<model_>::mean_post_process()
{
    const int r = this->m_coarse_graining_radius;

    tbb::parallel_for(tbb::blocked_range<size_t>(0, this->m_num_coarse_cells), [&](const tbb::blocked_range<size_t>& range) {
    for (size_t coarse_cell = range.begin(); coarse_cell != range.end(); ++coarse_cell)
    {
        // Get cell in the bottom left corner of the coarse cell
        const size_t cell = (coarse_cell % this->m_coarse_dim_x) * (2 * r)
                          + (coarse_cell / this->m_coarse_dim_x) * (2 * r) * this->m_dim_x;

        // Calculate the position of the cell in x direction
        int pos_x = cell % this->m_dim_x;

        // Initialize the coarse grained quantities to be computed
        Real mean_density    = 0.0;
        Real mean_momentum_x = 0.0;
        Real mean_momentum_y = 0.0;

        // Initialize the number of actual existing coarse graining neighbor cells
        int n_exist_neighbors = 0;

        // The thread working on the cell has to know the cell quantities of the coarse graining
        // neighbor cells, therefore looping over all neighbor cells and look it up
#pragma unroll
        for (int y = 0; y <= 2 * r; ++y) {

            for (int x = 0; x <= 2 * r; ++x) {

                // Get the index of the coarse graining neighbor cell
                size_t neighbor_idx = cell + y * this->m_dim_x + x;

                // Get the position of the coarse graining neighbor cell in x direction
                int pos_x_neighbor = neighbor_idx % this->m_dim_x;

                // Check weather the coarse graining neighbor cell is valid
                if ((neighbor_idx >= 0) &&
                    (neighbor_idx < this->m_num_cells) &&
                    (abs(pos_x_neighbor - pos_x) <= r)) {

                    // Increase the number of existing coarse graining neighbor cells
                    n_exist_neighbors++;

                    mean_density    += this->m_cell_density_cpu [neighbor_idx                        ];
                    mean_momentum_x += this->m_cell_momentum_cpu[neighbor_idx * this->SPATIAL_DIM    ];
                    mean_momentum_y += this->m_cell_momentum_cpu[neighbor_idx * this->SPATIAL_DIM + 1];
                }
            }
        }

        // Write the computed coarse grained quantities to the related data arrays
        this->m_mean_density_cpu [coarse_cell                        ] = mean_density    / ((Real) n_exist_neighbors);
        this->m_mean_momentum_cpu[coarse_cell * this->SPATIAL_DIM    ] = mean_momentum_x / ((Real) n_exist_neighbors);
        this->m_mean_momentum_cpu[coarse_cell * this->SPATIAL_DIM + 1] = mean_momentum_y / ((Real) n_exist_neighbors);

    }}); // for coarse_cell
}

// Allocates the memory for the arrays on the host (CPU)
template<Model model_>
void OMP_Lattice<model_>::allocate_memory()
{
    // Allocate host memory
    this->m_cell_type_cpu     = (CellType*)malloc(                    this->m_num_cells        * sizeof(CellType));
    this->m_cell_density_cpu  = (    Real*)malloc(                    this->m_num_cells        * sizeof(    Real));
    this->m_mean_density_cpu  = (    Real*)malloc(                    this->m_num_coarse_cells * sizeof(    Real));
    this->m_cell_momentum_cpu = (    Real*)malloc(this->SPATIAL_DIM * this->m_num_cells        * sizeof(    Real));
    this->m_mean_momentum_cpu = (    Real*)malloc(this->SPATIAL_DIM * this->m_num_coarse_cells * sizeof(    Real));

    this->m_node_state_cpu.resize    (this->m_num_cells * 8);
          m_node_state_tmp_cpu.resize(this->m_num_cells * 8);
    this->m_node_state_out_cpu.resize(this->m_num_cells * 8);
    this->m_rnd_cpu.resize           (this->m_num_cells);
}

// Frees the memory for the arrays on the host (CPU)
template<Model model_>
void OMP_Lattice<model_>::free_memory()
{
    // Free CPU memory
    free(this->m_cell_type_cpu);
    free(this->m_cell_density_cpu);
    free(this->m_mean_density_cpu);
    free(this->m_cell_momentum_cpu);
    free(this->m_mean_momentum_cpu);

    this->m_cell_type_cpu       = NULL;
    this->m_cell_density_cpu    = NULL;
    this->m_mean_density_cpu    = NULL;
    this->m_cell_momentum_cpu   = NULL;
    this->m_mean_momentum_cpu   = NULL;
}

// Sets (proper) parallelization parameters
template<Model model_>
void OMP_Lattice<model_>::setup_parallel()
{

#pragma omp parallel for
	for (unsigned int i = 0; i < omp_get_num_threads(); ++i)
	{
		if (omp_get_thread_num() == 0)
		{
			// Only execute in master thread.
            printf("OMP configuration parameters: Executing calculation with %d threads.\n\n", omp_get_num_threads());
		}
	}
}

// Computes the mean velocity of the lattice.
template<Model model_>
std::vector<Real> OMP_Lattice<model_>::get_mean_velocity() {

    std::vector<Real> mean_velocity(this->SPATIAL_DIM, 0.0);

    Real sum_x_vel = 0.0;
    Real sum_y_vel = 0.0;

    size_t counter = 0;

    // Sum up all (fluid) cell x and y velocity components.
#pragma omp parallel for reduction(+: sum_x_vel, sum_y_vel)
    for (size_t n = 0; n < this->m_num_cells; ++n) {

        if (this->m_cell_type_cpu[n] == CellType::FLUID) {

        	counter++;

            Real cell_density = this->m_cell_density_cpu[n];

			if (cell_density > 1.0e-06) {

                sum_x_vel += this->m_cell_momentum_cpu[n * this->SPATIAL_DIM    ] / cell_density;
                sum_y_vel += this->m_cell_momentum_cpu[n * this->SPATIAL_DIM + 1] / cell_density;
			}

#ifdef DEBUG

			else if (fabs(cell_density) < 1.0e-06) {

				// Do nothing.

			} else if (cell_density < -1.0e-06) {

				printf("ERROR in get_mean_velocity(): "
					   "Negative cell density detected.");
				abort();
			}

#endif

        }
    }

    // Divide the summed up x and y components by the total number of fluid cells.
    mean_velocity[0] = sum_x_vel / (Real) counter;
    mean_velocity[1] = sum_y_vel / (Real) counter;

    return mean_velocity;
}

// Explicit instantiations
template class OMP_Lattice<Model::HPP>;
template class OMP_Lattice<Model::FHP_I>;
template class OMP_Lattice<Model::FHP_II>;
template class OMP_Lattice<Model::FHP_III>;

} // namespace lgca
