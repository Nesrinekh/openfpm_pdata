/*
 * basic.hpp
 *
 *  Created on: Aug 8, 2015
 *      Author: i-bird
 */

#ifndef SRC_DECOMPOSITION_COMMON_HPP_
#define SRC_DECOMPOSITION_COMMON_HPP_

#include "Vector/map_vector.hpp"



/*! It contain a box definition and from witch sub-domain it come from (in the local processor)
 * and an unique across adjacent processors (for communication)
 *
 * If the box come from the intersection of an expanded sub-domain and a sub-domain
 *
 * Assuming we are considering the adjacent processor i (0 to getNNProcessors())
 *
 * ### external ghost box
 *
 * id = id_exp + N_non_exp + id_non_exp
 *
 * id_exp = the id in the vector proc_adj_box.get(i) of the expanded sub-domain
 *
 * id_non_exp = the id in the vector nn_processor_subdomains[i] of the sub-domain
 *
 * ### internal ghost box
 *
 * id = id_exp + N_non_exp + id_non_exp
 *
 * id_exp = the id in the vector nn_processor_subdomains[i] of the expanded sub-domain
 *
 * id_non_exp = the id in the vector proc_adj_box.get(i) of the sub-domain
 *
 */
template<unsigned int dim, typename T>
struct Box_sub : public Box<dim,T>
{
	// Domain id
	size_t sub;

	// Id
	size_t id;

	Box_sub operator=(const Box<dim,T> & box)
	{
		::Box<dim,T>::operator=(box);

		return *this;
	}


};

//! Particular case for local internal ghost boxes
template<unsigned int dim, typename T>
struct Box_sub_k : public Box<dim,T>
{
	// Domain id
	size_t sub;

	//! k \see getLocalGhostIBoxE
	long int k;

	Box_sub_k operator=(const Box<dim,T> & box)
	{
		::Box<dim,T>::operator=(box);

		return *this;
	}

	// encap interface to make compatible with OpenFPM_IO
	template <int i> auto get() -> decltype( std::declval<Box<dim,T> *>()->template get<i>())
	{
		return ::Box<dim,T>::template get<i>();
	}
};

//! Case for local ghost box
template<unsigned int dim, typename T>
struct lBox_dom
{
	// Intersection between the local sub-domain enlarged by the ghost and the contiguous processor
	// sub-domains (External ghost)
	openfpm::vector_std< Box_sub<dim,T> > ebx;

	// Intersection between the contiguous processor sub-domain enlarged by the ghost with the
	// local sub-domain (Internal ghost)
	openfpm::vector_std< Box_sub_k<dim,T>> ibx;
};

#endif /* SRC_DECOMPOSITION_COMMON_HPP_ */
