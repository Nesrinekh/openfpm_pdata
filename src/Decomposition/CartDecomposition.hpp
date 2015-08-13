/*
 * CartDecomposition.hpp
 *
 *  Created on: Aug 15, 2014
 *      Author: Pietro Incardona
 */

#ifndef CARTDECOMPOSITION_HPP
#define CARTDECOMPOSITION_HPP

#include "config.h"
#include "Decomposition.hpp"
#include "Vector/map_vector.hpp"
#include <vector>
#include "global_const.hpp"
#include <initializer_list>
#include "SubdomainGraphNodes.hpp"
#include "metis_util.hpp"
#include "dec_optimizer.hpp"
#include "Space/Shape/Box.hpp"
#include "Space/Shape/Point.hpp"
#include "NN/CellList/CellDecomposer.hpp"
#include <unordered_map>
#include "NN/CellList/CellList.hpp"
#include "Space/Ghost.hpp"
#include "common.hpp"
#include "ie_loc_ghost.hpp"
#include "ie_ghost.hpp"
#include "nn_processor.hpp"

/**
 * \brief This class decompose a space into subspaces
 *
 * \tparam dim is the dimensionality of the physical domain we are going to decompose.
 * \tparam T type of the space we decompose, Real, Integer, Complex ...
 * \tparam layout to use
 * \tparam Memory Memory factory used to allocate memory
 * \tparam Domain Structure that contain the information of your physical domain
 *
 * Given an N-dimensional space, this class decompose the space into a Cartesian grid of small
 * sub-sub-domain. At each sub-sub-domain is assigned  an id that identify which processor is
 * going to take care of that part of space (in general the space assigned to a processor is
 * simply connected), a second step merge several sub-sub-domain with same id into bigger region
 *  sub-domain with the id. Each sub-domain has an extended space called ghost part
 *
 * Assuming that VCluster.getProcessUnitID(), equivalent to the MPI processor rank, return the processor local
 * processor id, we define
 *
 * * local sub-domain: all the sub-domain with id == local processor
 * * external ghost box: (or ghost box) are the boxes that compose the ghost space of the processor, or the
 *   boxes produced expanding every local sub-domain by the ghost extension and intersecting with the sub-domain
 *   of the other processors
 * * Near processors are the processors adjacent to the local processor, where with adjacent we mean all the processor
 *   that has a non-zero intersection with the ghost part of the local processor, or all the processors that
 *   produce non-zero external boxes with the local processor, or all the processor that should communicate
 *   in case of ghost data synchronization
 * * internal ghost box: is the part of ghost of the near processor that intersect the space of the
 *       processor, or the boxes produced expanding the sub-domain of the near processors with the local sub-domain
 * * Near processor sub-domain: is a sub-domain that live in the a near (or contiguous) processor
 * * Near processor list: the list of all the near processor of the local processor (each processor has a list
 *                        of the near processor)
 * * Local ghosts interal or external are all the ghosts that does not involve inter-processor communications
 *
 * \see calculateGhostBoxes() for a visualization of internal and external ghost boxes
 *
 */

template<unsigned int dim, typename T, template<typename> class device_l=openfpm::device_cpu, typename Memory=HeapMemory, template<unsigned int, typename> class Domain=Box>
class CartDecomposition : public ie_loc_ghost<dim,T>, public nn_prcs<dim,T> , public ie_ghost<dim,T>
{

public:

	//! Type of the domain we are going to decompose
	typedef T domain_type;

	//! It simplify to access the SpaceBox element
	typedef SpaceBox<dim,T> Box;

private:

	//! This is the key type to access  data_s, for example in the case of vector
	//! acc_key is size_t
	typedef typename openfpm::vector<SpaceBox<dim,T>,device_l<SpaceBox<dim,T>>,Memory,openfpm::vector_grow_policy_default,openfpm::vect_isel<SpaceBox<dim,T>>::value >::access_key acc_key;

	//! the set of all local sub-domain as vector
	openfpm::vector<SpaceBox<dim,T>> sub_domains;

	//! for each sub-domain (first vector), contain the list (nested vector) of the neighborhood processors
	//! and for each processor contain the boxes calculated from the intersection
	//! of the sub-domains + ghost with the near-by processor sub-domain () and the other way around
	//! \see calculateGhostBoxes
//	openfpm::vector< openfpm::vector< Box_proc<dim,T> > > box_nn_processor_int;

	//! It store the same information of box_nn_processor_int organized by processor id
//	openfpm::vector< Box_dom<dim,T> > proc_int_box;

	//! for each sub-domain, contain the list of the neighborhood processors
	openfpm::vector<openfpm::vector<long unsigned int> > box_nn_processor;

	//! Structure that contain for each sub-sub-domain box the processor id
	//! exist for efficient global communication
	openfpm::vector<size_t> fine_s;

	//! Structure that store the cartesian grid information
	grid_sm<dim,void> gr;

	//! Structure that decompose your structure into cell without creating them
	//! useful to convert positions to CellId or sub-domain id in this case
	CellDecomposer_sm<dim,T> cd;

	//! rectangular domain to decompose
	Domain<dim,T> domain;

	//! Box Spacing
	T spacing[dim];

	//! Runtime virtual cluster machine
	Vcluster & v_cl;

	//! Cell-list that store the geometrical information of the internal ghost boxes
//	CellList<dim,T,FAST> geo_cell;

	//! Cell-list that store the geometrical information of the local internal ghost boxes
	CellList<dim,T,FAST> lgeo_cell;


	/*! \brief Create internally the decomposition
	 *
     * \param v_cl Virtual cluster, used internally to handle or pipeline communication
	 *
	 */
	void CreateDecomposition(Vcluster & v_cl)
	{
		// Calculate the total number of box and and the spacing
		// on each direction
		// Get the box containing the domain
		SpaceBox<dim,T> bs = domain.getBox();

		for (unsigned int i = 0; i < dim ; i++)
		{
			// Calculate the spacing
			spacing[i] = (bs.getHigh(i) - bs.getLow(i)) / gr.size(i);
		}

		// Here we use METIS
		// Create a cartesian grid graph
		CartesianGraphFactory<dim,Graph_CSR<nm_part_v,nm_part_e>> g_factory_part;

		// Processor graph
		Graph_CSR<nm_part_v,nm_part_e> gp = g_factory_part.template construct<NO_EDGE,T,dim-1>(gr.getSize(),domain);

		// Get the number of processing units
		size_t Np = v_cl.getProcessingUnits();

		// Get the processor id
		long int p_id = v_cl.getProcessUnitID();

		// Convert the graph to metis
		Metis<Graph_CSR<nm_part_v,nm_part_e>> met(gp,Np);

		// decompose
		met.decompose<nm_part_v::id>();

		// fill the structure that store the processor id for each sub-domain
		fine_s.resize(gr.size());

		// Optimize the decomposition creating bigger spaces
		// And reducing Ghost over-stress
		dec_optimizer<dim,Graph_CSR<nm_part_v,nm_part_e>> d_o(gp,gr.getSize());

		// set of Boxes produced by the decomposition optimizer
		openfpm::vector<::Box<dim,size_t>> loc_box;

		// optimize the decomposition
		d_o.template optimize<nm_part_v::sub_id,nm_part_v::id>(gp,p_id,loc_box,box_nn_processor);

		// Initialize ss_box and bbox
		if (loc_box.size() >= 0)
		{
			SpaceBox<dim,T> sub_d(loc_box.get(0));
			sub_d.mul(spacing);
			sub_d.expand(spacing);

			// Fixing sub_d
			// if (loc_box) is a the boundary we have to ensure that the box span the full
			// domain (avoiding rounding off error)
/*			for (size_t i = 0 ; i < dim ; i++)
			{
				if (loc_box.get(0).getHigh(i) == gr_cell.size(i) + 1)
				{
					loc_box.get(0).get
				}
			}*/

			// add the sub-domain
			sub_domains.add(sub_d);

			ss_box = sub_d;
			bbox = sub_d;
		}

		// convert into sub-domain
		for (size_t s = 1 ; s < loc_box.size() ; s++)
		{
			SpaceBox<dim,T> sub_d(loc_box.get(s));

			// re-scale and add spacing (the end is the starting point of the next domain + spacing)
			sub_d.mul(spacing);
			sub_d.expand(spacing);

			// add the sub-domain
			sub_domains.add(sub_d);

			// Calculate the bound box
			bbox.enclose(sub_d);

			// Create the smallest box contained in all sub-domain
			ss_box.contained(sub_d);
		}

		nn_prcs<dim,T>::create(box_nn_processor, sub_domains);

		// fill fine_s structure
		// fine_s structure contain the processor id for each sub-sub-domain
		// with sub-sub-domain we mean the sub-domain decomposition before
		// running dec_optimizer (before merging sub-domains)
		auto it = gp.getVertexIterator();

		while (it.isNext())
		{
			size_t key = it.get();

			// fill with the fine decomposition
			fine_s.get(key) = gp.template vertex_p<nm_part_v::id>(key);

			++it;
		}

		// Get the smallest sub-division on each direction
		::Box<dim,T> unit = getSmallestSubdivision();
		// Get the processor bounding Box
		::Box<dim,T> bound = getProcessorBounds();

		// calculate the sub-divisions (0.5 for rounding error)
		size_t div[dim];
		for (size_t i = 0 ; i < dim ; i++)
			div[i] = (size_t)((bound.getHigh(i) - bound.getLow(i)) / unit.getHigh(i) + 0.5);

		// Create shift
		Point<dim,T> orig;

		// p1 point of the Processor bound box is the shift
		for (size_t i = 0 ; i < dim ; i++)
			orig.get(i) = bound.getLow(i);

		// Initialize the geo_cell structure
		ie_ghost<dim,T>::Initialize_geo_cell(domain,div,orig);
		lgeo_cell.Initialize(domain,div,orig);
	}

	// Save the ghost boundaries
	Ghost<dim,T> ghost;


	/*! \brief Create the subspaces that decompose your domain
	 *
	 * Create the subspaces that decompose your domain
	 *
	 */

	void CreateSubspaces()
	{
		// Create a grid where each point is a space
		grid_sm<dim,void> g(div);

		// create a grid_key_dx iterator
		grid_key_dx_iterator<dim> gk_it(g);

		// Divide the space into subspaces
		while (gk_it.isNext())
		{
			//! iterate through all subspaces
			grid_key_dx<dim> key = gk_it.get();

			//! Create a new subspace
			SpaceBox<dim,T> tmp;

			//! fill with the Margin of the box
			for (int i = 0 ; i < dim ; i++)
			{
				tmp.setHigh(i,(key.get(i)+1)*spacing[i]);
				tmp.setLow(i,key.get(i)*spacing[i]);
			}

			//! add the space box
			sub_domains.add(tmp);

			// add the iterator
			++gk_it;
		}
	}

	/*! \brief Create the box_nn_processor_int (bx part)  structure
	 *
	 * This structure store for each sub-domain of this processors enlarged by the ghost size the boxes that
	 *  come from the intersection with the near processors sub-domains (External ghost box)
	 *
	 * \param ghost margins
	 *
	 * \note Are the G8_0 G9_0 G9_1 G5_0 boxes in calculateGhostBoxes
	 * \see calculateGhostBoxes
	 *
	 */
/*	void create_box_nn_processor_ext(Ghost<dim,T> & ghost)
	{
		box_nn_processor_int.resize(sub_domains.size());
		proc_int_box.resize(nn_prcs<dim,T>::getNNProcessors());

		// For each sub-domain
		for (size_t i = 0 ; i < sub_domains.size() ; i++)
		{
			SpaceBox<dim,T> sub_with_ghost = sub_domains.get(i);

			// enlarge the sub-domain with the ghost
			sub_with_ghost.enlarge(ghost);

			// resize based on the number of adjacent processors
			box_nn_processor_int.get(i).resize(box_nn_processor.get(i).size());

			// For each processor adjacent to this sub-domain
			for (size_t j = 0 ; j < box_nn_processor.get(i).size() ; j++)
			{
				// Contiguous processor
				size_t p_id = box_nn_processor.get(i).get(j);

				// store the box in proc_int_box storing from which sub-domain they come from
				Box_dom<dim,T> & proc_int_box_g = proc_int_box.get(nn_prcs<dim,T>::ProctoID(p_id));

				// get the set of sub-domains of the adjacent processor p_id
				const openfpm::vector< ::Box<dim,T> > & nn_processor_subdomains_g = nn_prcs<dim,T>::getAdjacentSubdomain(p_id);

				// near processor sub-domain intersections
				openfpm::vector< ::Box<dim,T> > & box_nn_processor_int_gg = box_nn_processor_int.get(i).get(j).bx;

				// for each near processor sub-domain intersect with the enlarged local sub-domain and store it
				for (size_t b = 0 ; b < nn_processor_subdomains_g.size() ; b++)
				{
					::Box<dim,T> bi;

					bool intersect = sub_with_ghost.Intersect(::Box<dim,T>(nn_processor_subdomains_g.get(b)),bi);

					if (intersect == true)
					{
						struct p_box<dim,T> pb;

						pb.box = bi;
						pb.proc = p_id;
						pb.lc_proc = nn_prcs<dim,T>::ProctoID(p_id);

						//
						// Updating
						//
						// vb_ext
						// box_nn_processor_int
						// proc_int_box
						//
						// They all store the same information but organized in different ways
						// read the description of each for more information
						//
						vb_ext.add(pb);
						box_nn_processor_int_gg.add(bi);
						proc_int_box_g.ebx.add();
						proc_int_box_g.ebx.last() = bi;
						proc_int_box_g.ebx.last().sub = i;

						// Search for the correct id
						size_t k = 0;
						size_t p_idp = nn_prcs<dim,T>::ProctoID(p_id);

						for (k = 0 ; k < nn_prcs<dim,T>::getInternalAdjSubdomain(p_idp).size() ; k++)
						{
							if (nn_prcs<dim,T>::getInternalAdjSubdomain(p_idp).get(k) == i)
								break;
						}
						if (k == nn_prcs<dim,T>::getInternalAdjSubdomain(p_idp).size())
							std::cerr << "Error: " << __FILE__ << ":" << __LINE__ << " sub-domain not found\n";

						proc_int_box_g.ebx.last().id = (k * nn_processor_subdomains_g.size() + b) * v_cl.getProcessingUnits() + p_id;
					}
				}
			}
		}
	}*/

	/*! \brief Create the box_nn_processor_int (nbx part) structure, the geo_cell list and proc_int_box
	 *
	 * This structure store for each sub-domain of this processors the boxes that come from the intersection
	 * of the near processors sub-domains enlarged by the ghost size (Internal ghost box). These boxes
	 * fill a geometrical cell list. The proc_int_box store the same information ordered by near processors
	 *
	 * \param ghost margins
	 *
	 * \note Are the B8_0 B9_0 B9_1 B5_0 boxes in calculateGhostBoxes
	 * \see calculateGhostBoxes
	 *
	 */
/*	void create_box_nn_processor_int(Ghost<dim,T> & ghost)
	{
		box_nn_processor_int.resize(sub_domains.size());
		proc_int_box.resize(nn_prcs<dim,T>::getNNProcessors());

		// For each sub-domain
		for (size_t i = 0 ; i < sub_domains.size() ; i++)
		{
			// For each processor contiguous to this sub-domain
			for (size_t j = 0 ; j < box_nn_processor.get(i).size() ; j++)
			{
				// Contiguous processor
				size_t p_id = box_nn_processor.get(i).get(j);

				// get the set of sub-domains of the contiguous processor p_id
				const openfpm::vector< ::Box<dim,T> > & nn_p_box = nn_prcs<dim,T>::getAdjacentSubdomain(p_id);

				// get the local processor id
				size_t lc_proc = nn_prcs<dim,T>::getAdjacentProcessor(p_id);

				// For each near processor sub-domains enlarge and intersect with the local sub-domain and store the result
				for (size_t k = 0 ; k < nn_p_box.size() ; k++)
				{

					// enlarge the near-processor sub-domain
					::Box<dim,T> n_sub = nn_p_box.get(k);

					// local sub-domain
					::SpaceBox<dim,T> l_sub = sub_domains.get(i);

					// Create a margin of ghost size around the near processor sub-domain
					n_sub.enlarge(ghost);

					// Intersect with the local sub-domain
					p_box<dim,T> b_int;
					bool intersect = n_sub.Intersect(l_sub,b_int.box);

					// store if it intersect
					if (intersect == true)
					{
						// the box fill with the processor id
						b_int.proc = p_id;

						// fill the local processor id
						b_int.lc_proc = lc_proc;

						//
						// Updating
						//
						// vb_int
						// box_nn_processor_int
						// proc_int_box
						//
						// They all store the same information but organized in different ways
						// read the description of each for more information
						//

						// add the box to the near processor sub-domain intersections
						openfpm::vector< ::Box<dim,T> > & p_box_int = box_nn_processor_int.get(i).get(j).nbx;
						p_box_int.add(b_int.box);
						vb_int.add(b_int);

						// store the box in proc_int_box storing from which sub-domain they come from
						Box_dom<dim,T> & pr_box_int = proc_int_box.get(nn_prcs<dim,T>::ProctoID(p_id));
						Box_sub<dim,T> sb;
						sb = b_int.box;
						sb.sub = i;

						// Search for the correct id
						size_t s = 0;
						size_t p_idp = nn_prcs<dim,T>::ProctoID(p_id);
						for (s = 0 ; s < nn_prcs<dim,T>::getInternalAdjSubdomain(p_idp).size() ; s++)
						{
							if (nn_prcs<dim,T>::getInternalAdjSubdomain(p_idp).get(s) == i)
								break;
						}
						if (s == nn_prcs<dim,T>::getInternalAdjSubdomain(p_idp).size())
							std::cerr << "Error: " << __FILE__ << ":" << __LINE__ << " sub-domain not found\n";

						sb.id = (k * nn_prcs<dim,T>::getInternalAdjSubdomain(p_idp).size() + s) * v_cl.getProcessingUnits() + v_cl.getProcessUnitID();

						pr_box_int.ibx.add(sb);

						// update the geo_cell list

						// get the cells this box span
						const grid_key_dx<dim> p1 = geo_cell.getCellGrid(b_int.box.getP1());
						const grid_key_dx<dim> p2 = geo_cell.getCellGrid(b_int.box.getP2());

						// Get the grid and the sub-iterator
						auto & gi = geo_cell.getGrid();
						grid_key_dx_iterator_sub<dim> g_sub(gi,p1,p2);

						// add the box-id to the cell list
						while (g_sub.isNext())
						{
							auto key = g_sub.get();
							geo_cell.addCell(gi.LinId(key),vb_int.size()-1);
							++g_sub;
						}
					}
				}
			}
		}
	}*/

	// Heap memory receiver
	HeapMemory hp_recv;

	// vector v_proc
	openfpm::vector<size_t> v_proc;

	// Receive counter
	size_t recv_cnt;

	/*! \brief Message allocation
	 *
	 * \param message size required to receive from i
	 * \param total message size to receive from all the processors
	 * \param the total number of processor want to communicate with you
	 * \param i processor id
	 * \param ri request id (it is an id that goes from 0 to total_p, and is unique
	 *           every time message_alloc is called)
	 * \param ptr a pointer to the vector_dist structure
	 *
	 * \return the pointer where to store the message
	 *
	 */
	static void * message_alloc(size_t msg_i ,size_t total_msg, size_t total_p, size_t i, size_t ri, void * ptr)
	{
		// cast the pointer
		CartDecomposition<dim,T,device_l,Memory,Domain> * cd = static_cast< CartDecomposition<dim,T,device_l,Memory,Domain> *>(ptr);

		// Resize the memory
		cd->nn_processor_subdomains[i].bx.resize(msg_i / sizeof(::Box<dim,T>) );

		// Return the receive pointer
		return cd->nn_processor_subdomains[i].bx.getPointer();
	}

public:

	/*! \brief Cartesian decomposition constructor
	 *
     * \param v_cl Virtual cluster, used internally to handle or pipeline communication
	 *
	 */
	CartDecomposition(Vcluster & v_cl)
	:nn_prcs<dim,T>(v_cl),v_cl(v_cl)
	{
		// Reset the box to zero
		bbox.zero();
	}

	//! Cartesian decomposition destructor
	~CartDecomposition()
	{}

//	openfpm::vector<size_t> ids;

	/*! \brief class to select the returned id by ghost_processorID
	 *
	 */
	class box_id
	{
	public:
		/*! \brief Return the box id
		 *
		 * \param p structure containing the id informations
		 * \param b_id box_id
		 *
		 * \return box id
		 *
		 */
		inline static size_t id(p_box<dim,T> & p, size_t b_id)
		{
			return b_id;
		}
	};

	/*! \brief class to select the returned id by ghost_processorID
	 *
	 */
	class processor_id
	{
	public:
		/*! \brief Return the processor id
		 *
		 * \param p structure containing the id informations
		 * \param b_id box_id
		 *
		 * \return processor id
		 *
		 */
		inline static size_t id(p_box<dim,T> & p, size_t b_id)
		{
			return p.proc;
		}
	};

	/*! \brief class to select the returned id by ghost_processorID
	 *
	 */
	class lc_processor_id
	{
	public:
		/*! \brief Return the near processor id
		 *
		 * \param p structure containing the id informations
		 * \param b_id box_id
		 *
		 * \return local processor id
		 *
		 */
		inline static size_t id(p_box<dim,T> & p, size_t b_id)
		{
			return p.lc_proc;
		}
	};

	/*! /brief Given a point it return the set of boxes in which the point fall
	 *
	 * \param p Point to check
	 * \return An iterator with the id's of the internal boxes in which the point fall
	 *
	 */
/*	auto getInternalIDBoxes(Point<dim,T> & p) -> decltype(geo_cell.getIterator(geo_cell.getCell(p)))
	{
		return geo_cell.getIterator(geo_cell.getCell(p));
	}*/


	/*! \brief Given a position it return if the position belong to any neighborhood processor ghost
	 * (Internal ghost)
	 *
	 * \tparam id type of if to get box_id processor_id lc_processor_id
	 * \param p Particle position
	 * \param opt intersection boxes of the same processor can overlap, so in general the function
	 *        can produce more entry with the same processor, the UNIQUE option eliminate double entries
	 *        (UNIQUE) is for particle data (MULTIPLE) is for grid data [default MULTIPLE]
	 *
	 * \param return the processor ids
	 *
	 */
/*	template <typename id> inline const openfpm::vector<size_t> ghost_processorID(Point<dim,T> & p, const int opt = MULTIPLE)
	{
		ids.clear();

		// Check with geo-cell if a particle is inside one Cell containing boxes

		auto cell_it = geo_cell.getIterator(geo_cell.getCell(p));

		// For each element in the cell, check if the point is inside the box
		// if it is, store the processor id
		while (cell_it.isNext())
		{
			size_t bid = cell_it.get();

			if (vb_int.get(bid).box.isInside(p) == true)
			{
				ids.add(id::id(vb_int.get(bid),bid));
			}

			++cell_it;
		}

		// Make the id unique
		if (opt == UNIQUE)
			ids.unique();

		return ids;
	}*/

	/*! \brief Given a position it return if the position belong to any neighborhood processor ghost
	 * (Internal ghost)
	 *
	 * \tparam id type of if to get box_id processor_id lc_processor_id
	 * \param p Particle position
	 *
	 * \param return the processor ids
	 *
	 */
/*	template<typename id, typename Mem> inline const openfpm::vector<size_t> ghost_processorID(const encapc<1,Point<dim,T>,Mem> & p, const int opt = MULTIPLE)
	{
		ids.clear();

		// Check with geo-cell if a particle is inside one Cell containing boxes

		auto cell_it = geo_cell.getIterator(geo_cell.getCell(p));

		// For each element in the cell, check if the point is inside the box
		// if it is, store the processor id
		while (cell_it.isNext())
		{
			size_t bid = cell_it.get();

			if (vb_int.get(bid).box.isInside(p) == true)
			{
				ids.add(id::id(vb_int.get(bid),bid));
			}

			++cell_it;
		}

		// Make the id unique
		if (opt == UNIQUE)
			ids.unique();

		return ids;
	}*/

	// External ghost boxes for this processor, indicated with G8_0 G9_0 ...
//	openfpm::vector<p_box<dim,T>> vb_ext;

	// Internal ghost boxes for this processor domain, indicated with B8_0 B9_0 ..... in the figure
	// below as a linear vector
//	openfpm::vector<p_box<dim,T>> vb_int;

	/*! It calculate the internal ghost boxes
	 *
	 * Example: Processor 10 calculate
	 * B8_0 B9_0 B9_1 and B5_0
	 *
	 *
+----------------------------------------------------+
|                                                    |
|                 Processor 8                        |
|                 Sub-domain 0                       +-----------------------------------+
|                                                    |                                   |
|                                                    |                                   |
++--------------+---+---------------------------+----+        Processor 9                |
 |              |   |     B8_0                  |    |        Subdomain 0                |
 |              +------------------------------------+                                   |
 |              |   |                           |    |                                   |
 |              |   |  XXXXXXXXXXXXX XX         |B9_0|                                   |
 |              | B |  X Processor 10 X         |    |                                   |
 | Processor 5  | 5 |  X Sub-domain 0 X         |    |                                   |
 | Subdomain 0  | _ |  X              X         +----------------------------------------+
 |              | 0 |  XXXXXXXXXXXXXXXX         |    |                                   |
 |              |   |                           |    |                                   |
 |              |   |                           |    |        Processor 9                |
 |              |   |                           |B9_1|        Subdomain 1                |
 |              |   |                           |    |                                   |
 |              |   |                           |    |                                   |
 |              |   |                           |    |                                   |
 +--------------+---+---------------------------+----+                                   |
                                                     |                                   |
                                                     +-----------------------------------+

       and also
       G8_0 G9_0 G9_1 G5_0 (External ghost boxes)

+----------------------------------------------------+
|                                                    |
|                 Processor 8                        |
|                 Sub-domain 0                       +-----------------------------------+
|           +---------------------------------------------+                              |
|           |         G8_0                           |    |                              |
++--------------+------------------------------------+    |   Processor 9                |
 |          |   |                                    |    |   Subdomain 0                |
 |          |   |                                    |G9_0|                              |
 |          |   |                                    |    |                              |
 |          |   |      XXXXXXXXXXXXX XX              |    |                              |
 |          |   |      X Processor 10 X              |    |                              |
 | Processor|5  |      X Sub-domain 0 X              |    |                              |
 | Subdomain|0  |      X              X              +-----------------------------------+
 |          |   |      XXXXXXXXXXXXXXXX              |    |                              |
 |          | G |                                    |    |                              |
 |          | 5 |                                    |    |   Processor 9                |
 |          | | |                                    |    |   Subdomain 1                |
 |          | 0 |                                    |G9_1|                              |
 |          |   |                                    |    |                              |
 |          |   |                                    |    |                              |
 +--------------+------------------------------------+    |                              |
            |                                        |    |                              |
            +----------------------------------------+----+------------------------------+


	 *
	 *
	 *
	 * \param ghost margins for each dimensions (p1 negative part) (p2 positive part)
	 *
                ^ p2[1]
                |
                |
           +----+----+
           |         |
           |         |
p1[0]<-----+         +----> p2[0]
           |         |
           |         |
           +----+----+
                |
                v  p1[1]

	 *
	 *
	 */
	void calculateGhostBoxes()
	{
#ifdef DEBUG
		// the ghost margins are assumed to be smaller
		// than one sub-domain

		for (size_t i = 0 ; i < dim ; i++)
		{
			if (ghost.template getLow(i) >= domain.template getHigh(i) / gr.size(i) || ghost.template getHigh(i)  >= domain.template getHigh(i) / gr.size(i))
			{
				std::cerr << "Error " << __FILE__ << ":" << __LINE__  << " : Ghost are bigger than one domain" << "\n";
			}
		}
#endif

		// Intersect all the local sub-domains with the sub-domains of the contiguous processors

		// create the internal structures that store ghost information
		ie_ghost<dim,T>::create_box_nn_processor_ext(v_cl,ghost,sub_domains,box_nn_processor,*this);
		ie_ghost<dim,T>::create_box_nn_processor_int(v_cl,ghost,sub_domains,box_nn_processor,*this);

		// ebox must come after ibox (in this case)
		ie_loc_ghost<dim,T>::create_loc_ghost_ibox(ghost,sub_domains);
		ie_loc_ghost<dim,T>::create_loc_ghost_ebox(ghost,sub_domains);
	}

	/*! \brief processorID return in which processor the particle should go
	 *
	 * \return processorID
	 *
	 */

	template<typename Mem> size_t inline processorID(encapc<1, Point<dim,T>, Mem> p)
	{
		return fine_s.get(cd.getCell(p));
	}

	// Smallest subdivision on each direction
	::Box<dim,T> ss_box;

	/*! \brief Get the smallest subdivision of the domain on each direction
	 *
	 * \return a box p1 is set to zero
	 *
	 */
	const ::Box<dim,T> & getSmallestSubdivision()
	{
		return ss_box;
	}

	/*! \brief processorID return in which processor the particle should go
	 *
	 * \return processorID
	 *
	 */

	size_t inline processorID(const T (&p)[dim]) const
	{
		return fine_s.get(cd.getCell(p));
	}

	/*! \brief Set the parameter of the decomposition
	 *
     * \param div_ storing into how many domain to decompose on each dimension
     * \param domain_ domain to decompose
	 *
	 */
	void setParameters(const size_t (& div_)[dim], Domain<dim,T> domain_, Ghost<dim,T> ghost = Ghost<dim,T>())
	{
		// set the ghost
		this->ghost = ghost;
		// Set the decomposition parameters

		gr.setDimensions(div_);
		domain = domain_;
		cd.setDimensions(domain,div_,0);

		//! Create the decomposition

		CreateDecomposition(v_cl);
	}

	/*! \brief Get the number of local local hyper-cubes or sub-domains
	 *
	 * \return the number of sub-domains
	 *
	 */
	size_t getNLocalHyperCube()
	{
		return sub_domains.size();
	}

	/*! \brief Get the number of one set of hyper-cube enclosing one particular
	 *         subspace, the hyper-cube enclose your space, even if one box is enough
	 *         can be more that one to increase occupancy
	 *
     * In case of Cartesian decomposition it just return 1, each subspace
	 * has one hyper-cube, and occupancy 1
	 *
	 * \param id of the subspace
	 * \return the number of hyper-cube enclosing your space
	 *
	 */
	size_t getNHyperCube(size_t id)
	{
		return 1;
	}

	/*! \brief Get the hyper-cube margins id_c has to be 0
	 *
	 * Get the hyper-cube margins id_c has to be 0, each subspace
	 * has one hyper-cube
	 *
	 * \param id of the subspace
	 * \param id_c
	 * \return The specified hyper-cube space
	 *
	 */
	SpaceBox<dim,T> & getHyperCubeMargins(size_t id, size_t id_c)
	{
#ifdef DEBUG
		// Check if this subspace exist
		if (id >= gr.size())
		{
			std::cerr << "Error CartDecomposition: id > N_tot";
		}
		else if (id_c > 0)
		{
			// Each subspace is an hyper-cube so return error if id_c > 0
			std::cerr << "Error CartDecomposition: id_c > 0";
		}
#endif

		return sub_domains.get<Object>(id);
	}

	/*! \brief Get the total number of sub-domain for the local processor
	 *
	 * \return The total number of sub-domains
	 *
	 */

	size_t getNHyperCube()
	{
		return gr.size();
	}

	/*! \brief Get the local sub-domain
	 *
	 * \param i (each local processor can have more than one sub-domain)
	 * \return the sub-domain
	 *
	 */
	SpaceBox<dim,T> getLocalHyperCube(size_t lc)
	{
		// Create a space box
		SpaceBox<dim,T> sp;

		// fill the space box

		for (size_t k = 0 ; k < dim ; k++)
		{
			// create the SpaceBox Low and High
			sp.setLow(k,sub_domains.template get<Box::p1>(lc)[k]);
			sp.setHigh(k,sub_domains.template get<Box::p2>(lc)[k]);
		}

		return sp;
	}

	/*! \brief Get the local sub-domain with ghost extension
	 *
	 * \param i (each local processor can have more than one sub-domain)
	 * \return the sub-domain
	 *
	 */

	SpaceBox<dim,T> getSubDomainWithGhost(size_t lc)
	{
		// Create a space box
		SpaceBox<dim,T> sp = sub_domains.get(lc);

		// enlarge with ghost
		sp.enlarge(ghost);

		return sp;
	}

	/*! \brief Return the structure that store the physical domain
	 *
	 * Return the structure that store the physical domain
	 *
	 * \return The physical domain
	 *
	 */

	Domain<dim,T> & getDomain()
	{
		return domain;
	}

	/*! \brief Check if the particle is local
	 *
	 * \param p object position
	 *
	 * \return true if it is local
	 *
	 */
	template<typename Mem> bool isLocal(const encapc<1, Point<dim,T>, Mem> p) const
	{
		return processorID<Mem>(p) == v_cl.getProcessUnitID();
	}

	/*! \brief Check if the particle is local
	 *
	 * \param p object position
	 *
	 * \return true if it is local
	 *
	 */
	bool isLocal(const T (&pos)[dim]) const
	{
		return processorID(pos) == v_cl.getProcessUnitID();
	}

	::Box<dim,T> bbox;

	/*! \brief Return the bounding box containing the processor box + smallest subdomain spacing
	 *
	 * \return The bounding box
	 *
	 */
	::Box<dim,T> & getProcessorBounds()
	{
		return bbox;
	}

	/*! \brief if the point fall into the ghost of some near processor it return the processors id's in which
	 *  it fall
	 *
	 * \param p Point
	 * \return iterator of the processors id's
	 *
	 */
/*	inline auto labelPoint(Point<dim,T> & p) -> decltype(geo_cell.getIterator(geo_cell.getCell(p)))
	{
		return geo_cell.getIterator(geo_cell.getCell(p));
	}*/


	////////////// Functions to get decomposition information ///////////////


	/*! \brief Get the number of Internal ghost boxes for one processor
	 *
	 * \param id near processor list id (the id go from 0 to getNNProcessor())
	 * \return the number of internal ghost
	 *
	 */
/*	inline size_t getProcessorNIGhost(size_t id) const
	{
		return proc_int_box.get(id).ibx.size();
	}*/

	/*! \brief Get the number of External ghost boxes for one processor id
	 *
	 * \param id near processor list id (the id go from 0 to getNNProcessor())
	 * \return the number of external ghost
	 *
	 */
/*	inline size_t getProcessorNEGhost(size_t id) const
	{
		return proc_int_box.get(id).ebx.size();
	}*/

	/*! \brief Get the j Internal ghost box for one processor
	 *
	 * \param id near processor list id (the id go from 0 to getNNProcessor())
	 * \param j box (each near processor can produce more than one internal ghost box)
	 * \return the box
	 *
	 */
/*	inline const ::Box<dim,T> & getProcessorIGhostBox(size_t id, size_t j) const
	{
		return proc_int_box.get(id).ibx.get(j);
	}*/

	/*! \brief Get the j External ghost box
	 *
	 * \param id near processor list id (the id go from 0 to getNNProcessor())
	 * \param j box (each near processor can produce more than one external ghost box)
	 * \return the box
	 *
	 */
/*	inline const ::Box<dim,T> & getProcessorEGhostBox(size_t id, size_t j) const
	{
		return proc_int_box.get(id).ebx.get(j);
	}*/

	/*! \brief Get the j Internal ghost box id
	 *
	 * \param id near processor list id (the id go from 0 to getNNProcessor())
	 * \param j box (each near processor can produce more than one internal ghost box)
	 * \return the box
	 *
	 */
/*	inline size_t getProcessorIGhostId(size_t id, size_t j) const
	{
		return proc_int_box.get(id).ibx.get(j).id;
	}*/

	/*! \brief Get the j External ghost box id
	 *
	 * \param id near processor list id (the id go from 0 to getNNProcessor())
	 * \param j box (each near processor can produce more than one external ghost box)
	 * \return the box
	 *
	 */
/*	inline size_t getProcessorEGhostId(size_t id, size_t j) const
	{
		return proc_int_box.get(id).ebx.get(j).id;
	}*/


	/*! \brief Get the local sub-domain at witch belong the internal ghost box
	 *
	 * \param id adjacent processor list id (the id go from 0 to getNNProcessor())
	 * \param j box (each near processor can produce more than one internal ghost box)
	 * \return sub-domain at which belong the internal ghost box
	 *
	 */
/*	inline const size_t getProcessorIGhostSub(size_t id, size_t j) const
	{
		return proc_int_box.get(id).ibx.get(j).sub;
	}*/

	/*! \brief Get the local sub-domain at witch belong the external ghost box
	 *
	 * \param id near processor list id (the id go from 0 to getNNProcessor())
	 * \param j box (each near processor can produce more than one external ghost box)
	 * \return sub-domain at which belong the external ghost box
	 *
	 */
/*	inline const size_t getProcessorEGhostSub(size_t id, size_t j) const
	{
		return proc_int_box.get(id).ebx.get(j).sub;
	}*/

	/*! \brief Return the total number of the calculated internal ghost boxes
	 *
	 * \return the number of internal ghost boxes
	 *
	 */
/*	inline size_t getNIGhostBox() const
	{
		return vb_int.size();
	}*/

	/*! \brief Given the internal ghost box id, it return the internal ghost box
	 *
	 * \return the internal ghost box
	 *
	 */
/*	inline ::Box<dim,T> getIGhostBox(size_t b_id) const
	{
		return vb_int.get(b_id).box;
	}*/

	/*! \brief Given the internal ghost box id, it return the near processor at witch belong
	 *         or the near processor that produced this internal ghost box
	 *
	 * \return the processor id of the ghost box
	 *
	 */
/*	inline size_t getIGhostBoxProcessor(size_t b_id) const
	{
		return vb_int.get(b_id).proc;
	}*/

	/*! \brief Get the number of the calculated external ghost boxes
	 *
	 * \return the number of external ghost boxes
	 *
	 */
/*	inline size_t getNEGhostBox() const
	{
		return vb_ext.size();
	}*/

	/*! \brief Given the external ghost box id, it return the external ghost box
	 *
	 * \return the external ghost box
	 *
	 */
/*	inline ::Box<dim,T> getEGhostBox(size_t b_id) const
	{
		return vb_ext.get(b_id).box;
	}*/

	/*! \brief Given the external ghost box id, it return the near processor at witch belong
	 *         or the near processor that produced this external ghost box
	 *
	 * \return the processor id of the external ghost box
	 *
	 */
/*	inline size_t getEGhostBoxProcessor(size_t b_id) const
	{
		return vb_ext.get(b_id).proc;
	}*/

	/*! \brief Write the decomposition as VTK file
	 *
	 * The function generate several files
	 *
	 * 1) subdomains_X.vtk domain for the local processor (X) as union of sub-domain
	 * 2) subdomains_adjacent_X.vtk sub-domains adjacent to the local processor (X)
	 * 3) internal_ghost_X.vtk Internal ghost boxes for the local processor (X)
	 * 4) external_ghost_X.vtk External ghost boxes for the local processor (X)
	 * 5) local_internal_ghost_X.vtk internal local ghost boxes for the local processor (X)
	 * 6) local_external_ghost_X.vtk external local ghost boxes for the local processor (X)
	 *
	 * where X is the local processor rank
	 *
	 * \param output directory where to write the files
	 *
	 */
	bool write(std::string output) const
	{
		//! subdomains_X.vtk domain for the local processor (X) as union of sub-domain
		VTKWriter<openfpm::vector<::SpaceBox<dim,T>>,VECTOR_BOX> vtk_box1;
		vtk_box1.add(sub_domains);
		vtk_box1.write(output + std::string("subdomains_") + std::to_string(v_cl.getProcessUnitID()) + std::string(".vtk"));

		nn_prcs<dim,T>::write(output);

		//! internal_ghost_X.vtk Internal ghost boxes for the local processor (X)
/*		VTKWriter<openfpm::vector<::Box<dim,T>>,VECTOR_BOX> vtk_box3;
		for (size_t p = 0 ; p < box_nn_processor_int.size() ; p++)
		{
			for (size_t s = 0 ; s < box_nn_processor_int.get(p).size() ; s++)
			{
				vtk_box3.add(box_nn_processor_int.get(p).get(s).nbx);
			}
		}
		vtk_box3.write(output + std::string("internal_ghost_") + std::to_string(v_cl.getProcessUnitID()) + std::string(".vtk"));

		//! external_ghost_X.vtk External ghost boxes for the local processor (X)
		VTKWriter<openfpm::vector<::Box<dim,T>>,VECTOR_BOX> vtk_box4;
		for (size_t p = 0 ; p < box_nn_processor_int.size() ; p++)
		{
			for (size_t s = 0 ; s < box_nn_processor_int.get(p).size() ; s++)
			{
				vtk_box4.add(box_nn_processor_int.get(p).get(s).bx);
			}
		}
		vtk_box4.write(output + std::string("external_ghost_") + std::to_string(v_cl.getProcessUnitID()) + std::string(".vtk"));*/

		ie_ghost<dim,T>::write(output,v_cl.getProcessUnitID());
		ie_loc_ghost<dim,T>::write(output,v_cl.getProcessUnitID());

		return true;
	}

	/*! \brief function to check the consistency of the information of the decomposition
	 *
	 * \return false if is inconsistent
	 *
	 */
	bool check_consistency()
	{
		if (ie_loc_ghost<dim,T>::check_consistency(getNLocalHyperCube()) == false)
			return false;

		return true;
	}

	void debugPrint()
	{
		std::cout << "External ghost box\n";

		for (size_t p = 0 ; p < nn_prcs<dim,T>::getNNProcessors() ; p++)
		{
			for (size_t i = 0 ; i < ie_ghost<dim,T>::getProcessorNEGhost(p) ; i++)
			{
				std::cout << ie_ghost<dim,T>::getProcessorEGhostBox(p,i).toString() << "   prc=" << nn_prcs<dim,T>::IDtoProc(p) << "   id=" << ie_ghost<dim,T>::getProcessorEGhostId(p,i) << "\n";
			}
		}

		std::cout << "Internal ghost box\n";

		for (size_t p = 0 ; p < nn_prcs<dim,T>::getNNProcessors() ; p++)
		{
			for (size_t i = 0 ; i < ie_ghost<dim,T>::getProcessorNIGhost(p) ; i++)
			{
				std::cout << ie_ghost<dim,T>::getProcessorIGhostBox(p,i).toString() << "   prc=" << nn_prcs<dim,T>::IDtoProc(p)  << "   id=" << ie_ghost<dim,T>::getProcessorIGhostId(p,i) <<  "\n";
			}
		}
	}
};


#endif
