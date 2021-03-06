/*
 * vector_dist_cell_list_tests.hpp
 *
 *  Created on: Aug 16, 2016
 *      Author: i-bird
 */

#ifndef SRC_VECTOR_VECTOR_DIST_CELL_LIST_TESTS_HPP_
#define SRC_VECTOR_VECTOR_DIST_CELL_LIST_TESTS_HPP_


///////////////////////// test hilb ///////////////////////////////

BOOST_AUTO_TEST_CASE( vector_dist_reorder_2d_test )
{
	Vcluster & v_cl = create_vcluster();

	if (v_cl.getProcessingUnits() > 48)
		return;

    // set the seed
	// create the random generator engine
	std::srand(v_cl.getProcessUnitID());
    std::default_random_engine eg;
    std::uniform_real_distribution<float> ud(0.0f, 1.0f);

    long int k = 524288 * v_cl.getProcessingUnits();

	long int big_step = k / 4;
	big_step = (big_step == 0)?1:big_step;

	print_test_v( "Testing 2D vector with hilbert curve reordering k<=",k);

	// 2D test
	for ( ; k >= 2 ; k-= decrement(k,big_step) )
	{
		BOOST_TEST_CHECKPOINT( "Testing 2D vector with hilbert curve reordering k=" << k );

		Box<2,float> box({0.0,0.0},{1.0,1.0});

		// Boundary conditions
		size_t bc[2]={NON_PERIODIC,NON_PERIODIC};

		vector_dist<2,float, Point_test<float>, CartDecomposition<2,float> > vd(k,box,bc,Ghost<2,float>(0.0));

		auto it = vd.getIterator();

		while (it.isNext())
		{
			auto key = it.get();

			vd.getPos(key)[0] = ud(eg);
			vd.getPos(key)[1] = ud(eg);

			++it;
		}

		vd.map();

		// Create first cell list

		auto NN1 = vd.getCellList(0.01);

		//An order of a curve
		int32_t m = 6;

		//Reorder a vector
		vd.reorder(m);

		// Create second cell list
		auto NN2 = vd.getCellList(0.01);

		//Check equality of cell sizes
		for (size_t i = 0 ; i < NN1.getGrid().size() ; i++)
		{
			size_t n1 = NN1.getNelements(i);
			size_t n2 = NN2.getNelements(i);

			BOOST_REQUIRE_EQUAL(n1,n2);
		}
	}
}

BOOST_AUTO_TEST_CASE( vector_dist_cl_random_vs_hilb_forces_test )
{
	Vcluster & v_cl = create_vcluster();

	if (v_cl.getProcessingUnits() > 48)
		return;

	///////////////////// INPUT DATA //////////////////////

	// Dimensionality of the space
	const size_t dim = 3;
	// Cut-off radiuses. Can be put different number of values
	openfpm::vector<float> cl_r_cutoff {0.05};
	// The starting amount of particles (remember that this number is multiplied by number of processors you use for testing)
	size_t cl_k_start = 10000;
	// The lower threshold for number of particles
	size_t cl_k_min = 1000;
	// Ghost part of distributed vector
	double ghost_part = 0.05;

	///////////////////////////////////////////////////////

	//For different r_cut
	for (size_t r = 0; r < cl_r_cutoff.size(); r++ )
	{
		//Cut-off radius
		float r_cut = cl_r_cutoff.get(r);

		//Number of particles
		size_t k = cl_k_start * v_cl.getProcessingUnits();

		std::string str("Testing " + std::to_string(dim) + "D vector's forces (random vs hilb celllist) k<=");

		vector_dist_test::print_test_v(str,k);

		//For different number of particles
		for (size_t k_int = k ; k_int >= cl_k_min ; k_int/=2 )
		{
			BOOST_TEST_CHECKPOINT( "Testing " << dim << "D vector's forces (random vs hilb celllist) k<=" << k_int );

			Box<dim,float> box;

			for (size_t i = 0; i < dim; i++)
			{
				box.setLow(i,0.0);
				box.setHigh(i,1.0);
			}

			// Boundary conditions
			size_t bc[dim];

			for (size_t i = 0; i < dim; i++)
				bc[i] = PERIODIC;

			vector_dist<dim,float, aggregate<float[dim]>, CartDecomposition<dim,float> > vd(k_int,box,bc,Ghost<dim,float>(ghost_part));

			vector_dist<dim,float, aggregate<float[dim]>, CartDecomposition<dim,float> > vd2(k_int,box,bc,Ghost<dim,float>(ghost_part));

			// Initialize dist vectors
			vd_initialize_double<dim>(vd, vd2, v_cl, k_int);

			vd.ghost_get<0>();
			vd2.ghost_get<0>();

			//Get a cell list

			auto NN = vd.getCellList(r_cut);

			//Calculate forces

			calc_forces<dim>(NN,vd,r_cut);

			//Get a cell list hilb

			auto NN_hilb = vd2.getCellList_hilb(r_cut);

			//Calculate forces
			calc_forces_hilb<dim>(NN_hilb,vd2,r_cut);

			// Calculate average
			size_t count = 1;
			Point<dim,float> avg;
			for (size_t i = 0 ; i < dim ; i++)	{avg.get(i) = 0.0;}

			auto it_v2 = vd.getIterator();
			while (it_v2.isNext())
			{
				//key
				vect_dist_key_dx key = it_v2.get();

				for (size_t i = 0; i < dim; i++)
					avg.get(i) += fabs(vd.getProp<0>(key)[i]);

				++count;
				++it_v2;
			}

			for (size_t i = 0 ; i < dim ; i++)	{avg.get(i) /= count;}

			auto it_v = vd.getIterator();
			while (it_v.isNext())
			{
				//key
				vect_dist_key_dx key = it_v.get();

				for (size_t i = 0; i < dim; i++)
				{
					auto a1 = vd.getProp<0>(key)[i];
					auto a2 = vd2.getProp<0>(key)[i];

					//Check that the forces are (almost) equal
					float per = 0.1;
					if (a1 != 0.0)
						per = fabs(0.1*avg.get(i)/a1);

					BOOST_REQUIRE_CLOSE(a1,a2,per);
				}

				++it_v;
			}
		}
	}
}

BOOST_AUTO_TEST_CASE( vector_dist_cl_random_vs_reorder_forces_test )
{
	Vcluster & v_cl = create_vcluster();

	if (v_cl.getProcessingUnits() > 48)
		return;

	///////////////////// INPUT DATA //////////////////////

	// Dimensionality of the space
	const size_t dim = 3;
	// Cut-off radiuses. Can be put different number of values
	openfpm::vector<float> cl_r_cutoff {0.01};
	// The starting amount of particles (remember that this number is multiplied by number of processors you use for testing)
	size_t cl_k_start = 10000;
	// The lower threshold for number of particles
	size_t cl_k_min = 1000;
	// Ghost part of distributed vector
	double ghost_part = 0.01;

	///////////////////////////////////////////////////////

	//For different r_cut
	for (size_t r = 0; r < cl_r_cutoff.size(); r++ )
	{
		//Cut-off radius
		float r_cut = cl_r_cutoff.get(r);

		//Number of particles
		size_t k = cl_k_start * v_cl.getProcessingUnits();

		std::string str("Testing " + std::to_string(dim) + "D vector's forces (random vs reorder) k<=");

		vector_dist_test::print_test_v(str,k);

		//For different number of particles
		for (size_t k_int = k ; k_int >= cl_k_min ; k_int/=2 )
		{
			BOOST_TEST_CHECKPOINT( "Testing " << dim << "D vector's forces (random vs reorder) k<=" << k_int );

			Box<dim,float> box;

			for (size_t i = 0; i < dim; i++)
			{
				box.setLow(i,0.0);
				box.setHigh(i,1.0);
			}

			// Boundary conditions
			size_t bc[dim];

			for (size_t i = 0; i < dim; i++)
				bc[i] = PERIODIC;

			vector_dist<dim,float, aggregate<float[dim], float[dim]>, CartDecomposition<dim,float> > vd(k_int,box,bc,Ghost<dim,float>(ghost_part));

			// Initialize vd
			vd_initialize<dim,decltype(vd)>(vd, v_cl, k_int);

			vd.ghost_get<0>();

			//Get a cell list

			auto NN1 = vd.getCellList(r_cut);

			//Calculate forces '0'

			calc_forces<dim>(NN1,vd,r_cut);

			//Reorder and get a cell list again

			vd.reorder(4);

			vd.ghost_get<0>();

			auto NN2 = vd.getCellList(r_cut);

			//Calculate forces '1'
			calc_forces<dim,1>(NN2,vd,r_cut);

			// Calculate average (For Coverty scan we start from 1)
			size_t count = 1;
			Point<dim,float> avg;
			for (size_t i = 0 ; i < dim ; i++)	{avg.get(i) = 0.0;}

			auto it_v2 = vd.getIterator();
			while (it_v2.isNext())
			{
				//key
				vect_dist_key_dx key = it_v2.get();

				for (size_t i = 0; i < dim; i++)
					avg.get(i) += fabs(vd.getProp<0>(key)[i]);

				++count;
				++it_v2;
			}

			for (size_t i = 0 ; i < dim ; i++)	{avg.get(i) /= count;}

			//Test for equality of forces
			auto it_v = vd.getDomainIterator();

			while (it_v.isNext())
			{
				//key
				vect_dist_key_dx key = it_v.get();

				for (size_t i = 0; i < dim; i++)
				{
					auto a1 = vd.getProp<0>(key)[i];
					auto a2 = vd.getProp<1>(key)[i];

					//Check that the forces are (almost) equal
					float per = 0.1;
					if (a1 != 0.0)
						per = fabs(0.1*avg.get(i)/a1);

					BOOST_REQUIRE_CLOSE(a1,a2,per);
				}

				++it_v;
			}
		}
	}
}

BOOST_AUTO_TEST_CASE( vector_dist_symmetric_cell_list )
{
	Vcluster & v_cl = create_vcluster();

	if (v_cl.getProcessingUnits() > 24)
		return;

	float L = 1000.0;

    // set the seed
	// create the random generator engine
	std::srand(0);
    std::default_random_engine eg;
    std::uniform_real_distribution<float> ud(-L,L);

    long int k = 4096 * v_cl.getProcessingUnits();

	long int big_step = k / 4;
	big_step = (big_step == 0)?1:big_step;

	print_test("Testing 3D periodic vector symmetric cell-list k=",k);
	BOOST_TEST_CHECKPOINT( "Testing 3D periodic vector symmetric cell-list k=" << k );

	Box<3,float> box({-L,-L,-L},{L,L,L});

	// Boundary conditions
	size_t bc[3]={PERIODIC,PERIODIC,PERIODIC};

	float r_cut = 100.0;

	// ghost
	Ghost<3,float> ghost(r_cut);

	// Point and global id
	struct point_and_gid
	{
		size_t id;
		Point<3,float> xq;

		bool operator<(const struct point_and_gid & pag) const
		{
			return (id < pag.id);
		}
	};

	typedef  aggregate<size_t,size_t,size_t,openfpm::vector<point_and_gid>,openfpm::vector<point_and_gid>> part_prop;

	// Distributed vector
	vector_dist<3,float, part_prop > vd(k,box,bc,ghost);
	size_t start = vd.init_size_accum(k);

	auto it = vd.getIterator();

	while (it.isNext())
	{
		auto key = it.get();

		vd.getPos(key)[0] = ud(eg);
		vd.getPos(key)[1] = ud(eg);
		vd.getPos(key)[2] = ud(eg);

		// Fill some properties randomly

		vd.getProp<0>(key) = 0;
		vd.getProp<1>(key) = 0;
		vd.getProp<2>(key) = key.getKey() + start;

		++it;
	}

	vd.map();

	// sync the ghost
	vd.ghost_get<0,2>();

	auto NN = vd.getCellList(r_cut);
	auto p_it = vd.getDomainIterator();

	while (p_it.isNext())
	{
		auto p = p_it.get();

		Point<3,float> xp = vd.getPos(p);

		auto Np = NN.getNNIterator(NN.getCell(vd.getPos(p)));

		while (Np.isNext())
		{
			auto q = Np.get();

			if (p.getKey() == q)
			{
				++Np;
				continue;
			}

			// repulsive

			Point<3,float> xq = vd.getPos(q);
			Point<3,float> f = (xp - xq);

			float distance = f.norm();

			// Particle should be inside 2 * r_cut range

			if (distance < r_cut )
			{
				vd.getProp<0>(p)++;
				vd.getProp<3>(p).add();
				vd.getProp<3>(p).last().xq = xq;
				vd.getProp<3>(p).last().id = vd.getProp<2>(q);
			}

			++Np;
		}

		++p_it;
	}

	// We now try symmetric  Cell-list

	auto NN2 = vd.getCellListSym(r_cut);

	auto p_it2 = vd.getDomainIterator();

	while (p_it2.isNext())
	{
		auto p = p_it2.get();

		Point<3,float> xp = vd.getPos(p);

		auto Np = NN2.getNNIteratorSym<NO_CHECK>(NN2.getCell(vd.getPos(p)),p.getKey(),vd.getPosVector());

		while (Np.isNext())
		{
			auto q = Np.get();

			if (p.getKey() == q)
			{
				++Np;
				continue;
			}

			// repulsive

			Point<3,float> xq = vd.getPos(q);
			Point<3,float> f = (xp - xq);

			float distance = f.norm();

			// Particle should be inside r_cut range

			if (distance < r_cut )
			{
				vd.getProp<1>(p)++;
				vd.getProp<1>(q)++;

				vd.getProp<4>(p).add();
				vd.getProp<4>(q).add();

				vd.getProp<4>(p).last().xq = xq;
				vd.getProp<4>(q).last().xq = xp;
				vd.getProp<4>(p).last().id = vd.getProp<2>(q);
				vd.getProp<4>(q).last().id = vd.getProp<2>(p);
			}

			++Np;
		}

		++p_it2;
	}

	vd.ghost_put<add_,1>();
	vd.ghost_put<merge_,4>();

	auto p_it3 = vd.getDomainIterator();

	bool ret = true;
	while (p_it3.isNext())
	{
		auto p = p_it3.get();

		ret &= vd.getProp<1>(p) == vd.getProp<0>(p);

		vd.getProp<3>(p).sort();
		vd.getProp<4>(p).sort();

		ret &= vd.getProp<3>(p).size() == vd.getProp<4>(p).size();

		for (size_t i = 0 ; i < vd.getProp<3>(p).size() ; i++)
			ret &= vd.getProp<3>(p).get(i).id == vd.getProp<4>(p).get(i).id;

		if (ret == false)
			break;

		++p_it3;
	}

	BOOST_REQUIRE_EQUAL(ret,true);
}

BOOST_AUTO_TEST_CASE( vector_dist_symmetric_crs_cell_list )
{
	Vcluster & v_cl = create_vcluster();

	if (v_cl.getProcessingUnits() > 24)
		return;

	float L = 1000.0;

    // set the seed
	// create the random generator engine
    std::default_random_engine eg(1132312*v_cl.getProcessUnitID());
    std::uniform_real_distribution<float> ud(-L,L);

    long int k = 4096 * v_cl.getProcessingUnits();

	long int big_step = k / 4;
	big_step = (big_step == 0)?1:big_step;

	print_test("Testing 3D periodic vector symmetric crs cell-list k=",k);
	BOOST_TEST_CHECKPOINT( "Testing 3D periodic vector symmetric crs cell-list k=" << k );

	Box<3,float> box({-L,-L,-L},{L,L,L});

	// Boundary conditions
	size_t bc[3]={PERIODIC,PERIODIC,PERIODIC};

	float r_cut = 100.0;

	// ghost
	Ghost<3,float> ghost(r_cut);
	Ghost<3,float> ghost2(r_cut);
	ghost2.setLow(0,0.0);
	ghost2.setLow(1,0.0);
	ghost2.setLow(2,0.0);

	// Point and global id
	struct point_and_gid
	{
		size_t id;
		Point<3,float> xq;

		bool operator<(const struct point_and_gid & pag) const
		{
			return (id < pag.id);
		}
	};

	typedef  aggregate<size_t,size_t,size_t,openfpm::vector<point_and_gid>,openfpm::vector<point_and_gid>> part_prop;

	// Distributed vector
	vector_dist<3,float, part_prop > vd(k,box,bc,ghost,BIND_DEC_TO_GHOST);
	vector_dist<3,float, part_prop > vd2(k,box,bc,ghost2,BIND_DEC_TO_GHOST);
	size_t start = vd.init_size_accum(k);

	auto it = vd.getIterator();

	while (it.isNext())
	{
		auto key = it.get();

		vd.getPos(key)[0] = ud(eg);
		vd.getPos(key)[1] = ud(eg);
		vd.getPos(key)[2] = ud(eg);

		vd2.getPos(key)[0] = vd.getPos(key)[0];
		vd2.getPos(key)[1] = vd.getPos(key)[1];
		vd2.getPos(key)[2] = vd.getPos(key)[2];

		// Fill some properties randomly

		vd.getProp<0>(key) = 0;
		vd.getProp<1>(key) = 0;
		vd.getProp<2>(key) = key.getKey() + start;

		vd2.getProp<0>(key) = 0;
		vd2.getProp<1>(key) = 0;
		vd2.getProp<2>(key) = key.getKey() + start;

		++it;
	}

	vd.map();
	vd2.map();

	// sync the ghost
	vd.ghost_get<0,2>();
	vd2.ghost_get<0,2>();

	vd2.write("CRS_output");
	vd2.getDecomposition().write("CRS_output_dec");

	auto NN = vd.getCellList(r_cut);
	auto p_it = vd.getDomainIterator();

	while (p_it.isNext())
	{
		auto p = p_it.get();

		Point<3,float> xp = vd.getPos(p);

		auto Np = NN.getNNIterator(NN.getCell(vd.getPos(p)));

		while (Np.isNext())
		{
			auto q = Np.get();

			if (p.getKey() == q)
			{
				++Np;
				continue;
			}

			// repulsive

			Point<3,float> xq = vd.getPos(q);
			Point<3,float> f = (xp - xq);

			float distance = f.norm();

			// Particle should be inside 2 * r_cut range

			if (distance < r_cut )
			{
				vd.getProp<0>(p)++;
				vd.getProp<3>(p).add();
				vd.getProp<3>(p).last().xq = xq;
				vd.getProp<3>(p).last().id = vd.getProp<2>(q);
			}

			++Np;
		}

		++p_it;
	}

	// We now try symmetric  Cell-list

	auto NN2 = vd2.getCellListSym(r_cut);

	// In case of CRS we have to iterate particles within some cells
	// here we define whichone
	auto p_it2 = vd2.getParticleIteratorCRS(NN2);

	// For each particle
	while (p_it2.isNext())
	{
		auto p = p_it2.get();

		Point<3,float> xp = vd2.getPos(p);

		auto Np = p_it2.getNNIteratorCSR(vd2.getPosVector());

		while (Np.isNext())
		{
			auto q = Np.get();

			if (p == q)
			{
				++Np;
				continue;
			}

			// repulsive

			Point<3,float> xq = vd2.getPos(q);
			Point<3,float> f = (xp - xq);

			float distance = f.norm();

			// Particle should be inside r_cut range

			if (distance < r_cut )
			{
				vd2.getProp<1>(p)++;
				vd2.getProp<1>(q)++;

				vd2.getProp<4>(p).add();
				vd2.getProp<4>(q).add();

				vd2.getProp<4>(p).last().xq = xq;
				vd2.getProp<4>(q).last().xq = xp;
				vd2.getProp<4>(p).last().id = vd2.getProp<2>(q);
				vd2.getProp<4>(q).last().id = vd2.getProp<2>(p);
			}

			++Np;
		}

		++p_it2;
	}

	vd2.ghost_put<add_,1>(NO_CHANGE_ELEMENTS);
	vd2.ghost_put<merge_,4>();

	auto p_it3 = vd.getDomainIterator();

	bool ret = true;
	while (p_it3.isNext())
	{
		auto p = p_it3.get();

		ret &= vd2.getProp<1>(p) == vd.getProp<0>(p);


		vd.getProp<3>(p).sort();
		vd2.getProp<4>(p).sort();

		ret &= vd.getProp<3>(p).size() == vd2.getProp<4>(p).size();

		for (size_t i = 0 ; i < vd.getProp<3>(p).size() ; i++)
			ret &= vd.getProp<3>(p).get(i).id == vd2.getProp<4>(p).get(i).id;

		if (ret == false)
			break;

		++p_it3;
	}

	BOOST_REQUIRE_EQUAL(ret,true);
}

BOOST_AUTO_TEST_CASE( vector_dist_symmetric_verlet_list )
{
	Vcluster & v_cl = create_vcluster();

	if (v_cl.getProcessingUnits() > 24)
		return;

	float L = 1000.0;

    // set the seed
	// create the random generator engine
	std::srand(0);
    std::default_random_engine eg;
    std::uniform_real_distribution<float> ud(-L,L);

    long int k = 4096 * v_cl.getProcessingUnits();

	long int big_step = k / 4;
	big_step = (big_step == 0)?1:big_step;

	print_test("Testing 3D periodic vector symmetric cell-list k=",k);
	BOOST_TEST_CHECKPOINT( "Testing 3D periodic vector symmetric cell-list k=" << k );

	Box<3,float> box({-L,-L,-L},{L,L,L});

	// Boundary conditions
	size_t bc[3]={PERIODIC,PERIODIC,PERIODIC};

	float r_cut = 100.0;

	// ghost
	Ghost<3,float> ghost(r_cut);

	// Point and global id
	struct point_and_gid
	{
		size_t id;
		Point<3,float> xq;

		bool operator<(const struct point_and_gid & pag) const
		{
			return (id < pag.id);
		}
	};

	typedef  aggregate<size_t,size_t,size_t,openfpm::vector<point_and_gid>,openfpm::vector<point_and_gid>> part_prop;

	// Distributed vector
	vector_dist<3,float, part_prop > vd(k,box,bc,ghost);
	size_t start = vd.init_size_accum(k);

	auto it = vd.getIterator();

	while (it.isNext())
	{
		auto key = it.get();

		vd.getPos(key)[0] = ud(eg);
		vd.getPos(key)[1] = ud(eg);
		vd.getPos(key)[2] = ud(eg);

		// Fill some properties randomly

		vd.getProp<0>(key) = 0;
		vd.getProp<1>(key) = 0;
		vd.getProp<2>(key) = key.getKey() + start;

		++it;
	}

	vd.map();

	// sync the ghost
	vd.ghost_get<0,2>();

	auto NN = vd.getVerlet(r_cut);
	auto p_it = vd.getDomainIterator();

	while (p_it.isNext())
	{
		auto p = p_it.get();

		Point<3,float> xp = vd.getPos(p);

		auto Np = NN.getNNIterator(p.getKey());

		while (Np.isNext())
		{
			auto q = Np.get();

			if (p.getKey() == q)
			{
				++Np;
				continue;
			}

			// repulsive

			Point<3,float> xq = vd.getPos(q);
			Point<3,float> f = (xp - xq);

			float distance = f.norm();

			// Particle should be inside 2 * r_cut range

			if (distance < r_cut )
			{
				vd.getProp<0>(p)++;
				vd.getProp<3>(p).add();
				vd.getProp<3>(p).last().xq = xq;
				vd.getProp<3>(p).last().id = vd.getProp<2>(q);
			}

			++Np;
		}

		++p_it;
	}

	// We now try symmetric  Cell-list

	auto NN2 = vd.getVerletSym(r_cut);

	auto p_it2 = vd.getDomainIterator();

	while (p_it2.isNext())
	{
		auto p = p_it2.get();

		Point<3,float> xp = vd.getPos(p);

		auto Np = NN2.getNNIterator<NO_CHECK>(p.getKey());

		while (Np.isNext())
		{
			auto q = Np.get();

			if (p.getKey() == q)
			{
				++Np;
				continue;
			}

			// repulsive

			Point<3,float> xq = vd.getPos(q);
			Point<3,float> f = (xp - xq);

			float distance = f.norm();

			// Particle should be inside r_cut range

			if (distance < r_cut )
			{
				vd.getProp<1>(p)++;
				vd.getProp<1>(q)++;

				vd.getProp<4>(p).add();
				vd.getProp<4>(q).add();

				vd.getProp<4>(p).last().xq = xq;
				vd.getProp<4>(q).last().xq = xp;
				vd.getProp<4>(p).last().id = vd.getProp<2>(q);
				vd.getProp<4>(q).last().id = vd.getProp<2>(p);
			}

			++Np;
		}

		++p_it2;
	}

	vd.ghost_put<add_,1>();
	vd.ghost_put<merge_,4>();

	auto p_it3 = vd.getDomainIterator();

	bool ret = true;
	while (p_it3.isNext())
	{
		auto p = p_it3.get();

		ret &= vd.getProp<1>(p) == vd.getProp<0>(p);

		vd.getProp<3>(p).sort();
		vd.getProp<4>(p).sort();

		ret &= vd.getProp<3>(p).size() == vd.getProp<4>(p).size();

		for (size_t i = 0 ; i < vd.getProp<3>(p).size() ; i++)
			ret &= vd.getProp<3>(p).get(i).id == vd.getProp<4>(p).get(i).id;

		if (ret == false)
			break;

		++p_it3;
	}

	BOOST_REQUIRE_EQUAL(ret,true);
}

BOOST_AUTO_TEST_CASE( vector_dist_symmetric_verlet_list_no_bottom )
{
	Vcluster & v_cl = create_vcluster();

	if (v_cl.getProcessingUnits() > 24)
		return;

	float L = 1000.0;

    // set the seed
	// create the random generator engine
	std::srand(0);
    std::default_random_engine eg;
    std::uniform_real_distribution<float> ud(-L,L);

    long int k = 4096 * v_cl.getProcessingUnits();

	long int big_step = k / 4;
	big_step = (big_step == 0)?1:big_step;

	print_test("Testing 3D periodic vector symmetric cell-list no bottom k=",k);
	BOOST_TEST_CHECKPOINT( "Testing 3D periodic vector symmetric cell-list no bottom k=" << k );

	Box<3,float> box({-L,-L,-L},{L,L,L});

	// Boundary conditions
	size_t bc[3]={PERIODIC,PERIODIC,PERIODIC};

	float r_cut = 100.0;

	// ghost
	Ghost<3,float> ghost(r_cut);
	Ghost<3,float> ghost2(r_cut);
	ghost2.setLow(2,0.0);

	// Point and global id
	struct point_and_gid
	{
		size_t id;
		Point<3,float> xq;

		bool operator<(const struct point_and_gid & pag) const
		{
			return (id < pag.id);
		}
	};

	typedef  aggregate<size_t,size_t,size_t,openfpm::vector<point_and_gid>,openfpm::vector<point_and_gid>> part_prop;

	// 3D test
	for (size_t s = 0 ; s < 8 ; s++)
	{

		// Distributed vector
		vector_dist<3,float, part_prop > vd(k,box,bc,ghost,BIND_DEC_TO_GHOST);
		vector_dist<3,float, part_prop > vd2(k,box,bc,ghost2,BIND_DEC_TO_GHOST);
		size_t start = vd.init_size_accum(k);

		auto it = vd.getIterator();

		while (it.isNext())
		{
			auto key = it.get();

			vd.getPos(key)[0] = ud(eg);
			vd.getPos(key)[1] = ud(eg);
			vd.getPos(key)[2] = ud(eg);

			vd2.getPos(key)[0] = vd.getPos(key)[0];
			vd2.getPos(key)[1] = vd.getPos(key)[1];
			vd2.getPos(key)[2] = vd.getPos(key)[2];

			// Fill some properties randomly

			vd.getProp<0>(key) = 0;
			vd.getProp<1>(key) = 0;
			vd.getProp<2>(key) = key.getKey() + start;

			vd2.getProp<0>(key) = 0;
			vd2.getProp<1>(key) = 0;
			vd2.getProp<2>(key) = key.getKey() + start;

			++it;
		}

		vd.map();
		vd2.map();

		// sync the ghost
		vd.ghost_get<0,2>();
		vd2.ghost_get<0,2>();

		auto NN = vd.getVerlet(r_cut);
		auto p_it = vd.getDomainIterator();

		while (p_it.isNext())
		{
			auto p = p_it.get();

			Point<3,float> xp = vd.getPos(p);

			auto Np = NN.getNNIterator(p.getKey());

			while (Np.isNext())
			{
				auto q = Np.get();

				if (p.getKey() == q)
				{
					++Np;
					continue;
				}

				// repulsive

				Point<3,float> xq = vd.getPos(q);
				Point<3,float> f = (xp - xq);

				float distance = f.norm();

				// Particle should be inside 2 * r_cut range

				if (distance < r_cut )
				{
					vd.getProp<0>(p)++;
					vd.getProp<3>(p).add();
					vd.getProp<3>(p).last().xq = xq;
					vd.getProp<3>(p).last().id = vd.getProp<2>(q);
				}

				++Np;
			}

			++p_it;
		}

		// We now try symmetric  Cell-list

		auto NN2 = vd2.getVerletSym(r_cut);

		auto p_it2 = vd2.getDomainIterator();

		while (p_it2.isNext())
		{
			auto p = p_it2.get();

			Point<3,float> xp = vd2.getPos(p);

			auto Np = NN2.getNNIterator<NO_CHECK>(p.getKey());

			while (Np.isNext())
			{
				auto q = Np.get();

				if (p.getKey() == q)
				{
					++Np;
					continue;
				}

				// repulsive

				Point<3,float> xq = vd2.getPos(q);
				Point<3,float> f = (xp - xq);

				float distance = f.norm();

				// Particle should be inside r_cut range

				if (distance < r_cut )
				{
					vd2.getProp<1>(p)++;
					vd2.getProp<1>(q)++;

					vd2.getProp<4>(p).add();
					vd2.getProp<4>(q).add();

					vd2.getProp<4>(p).last().xq = xq;
					vd2.getProp<4>(q).last().xq = xp;
					vd2.getProp<4>(p).last().id = vd2.getProp<2>(q);
					vd2.getProp<4>(q).last().id = vd2.getProp<2>(p);
				}

				++Np;
			}


			++p_it2;
		}

		vd2.ghost_put<add_,1>();
		vd2.ghost_put<merge_,4>();

		auto p_it3 = vd.getDomainIterator();

		bool ret = true;
		while (p_it3.isNext())
		{
			auto p = p_it3.get();

			ret &= vd2.getProp<1>(p) == vd.getProp<0>(p);


			vd.getProp<3>(p).sort();
			vd2.getProp<4>(p).sort();

			ret &= vd.getProp<3>(p).size() == vd2.getProp<4>(p).size();

			for (size_t i = 0 ; i < vd.getProp<3>(p).size() ; i++)
				ret &= vd.getProp<3>(p).get(i).id == vd2.getProp<4>(p).get(i).id;

			if (ret == false)
				break;

			++p_it3;
		}

		BOOST_REQUIRE_EQUAL(ret,true);
	}
}


BOOST_AUTO_TEST_CASE( vector_dist_symmetric_crs_verlet_list )
{
	Vcluster & v_cl = create_vcluster();

	if (v_cl.getProcessingUnits() > 24)
		return;

	float L = 1000.0;

    // set the seed
	// create the random generator engine
	std::srand(0);
    std::default_random_engine eg;
    std::uniform_real_distribution<float> ud(-L,L);

    long int k = 4096 * v_cl.getProcessingUnits();

	long int big_step = k / 4;
	big_step = (big_step == 0)?1:big_step;

	print_test("Testing 3D periodic vector symmetric cell-list k=",k);
	BOOST_TEST_CHECKPOINT( "Testing 3D periodic vector symmetric cell-list k=" << k );

	Box<3,float> box({-L,-L,-L},{L,L,L});

	// Boundary conditions
	size_t bc[3]={PERIODIC,PERIODIC,PERIODIC};

	float r_cut = 100.0;

	// ghost
	Ghost<3,float> ghost(r_cut);
	Ghost<3,float> ghost2(r_cut);
	ghost2.setLow(0,0.0);
	ghost2.setLow(1,0.0);
	ghost2.setLow(2,0.0);

	// Point and global id
	struct point_and_gid
	{
		size_t id;
		Point<3,float> xq;

		bool operator<(const struct point_and_gid & pag) const
		{
			return (id < pag.id);
		}
	};

	typedef  aggregate<size_t,size_t,size_t,openfpm::vector<point_and_gid>,openfpm::vector<point_and_gid>> part_prop;

	// Distributed vector
	vector_dist<3,float, part_prop > vd(k,box,bc,ghost,BIND_DEC_TO_GHOST);
	vector_dist<3,float, part_prop > vd2(k,box,bc,ghost2,BIND_DEC_TO_GHOST);
	size_t start = vd.init_size_accum(k);

	auto it = vd.getIterator();

	while (it.isNext())
	{
		auto key = it.get();

		vd.getPos(key)[0] = ud(eg);
		vd.getPos(key)[1] = ud(eg);
		vd.getPos(key)[2] = ud(eg);

		vd2.getPos(key)[0] = vd.getPos(key)[0];
		vd2.getPos(key)[1] = vd.getPos(key)[1];
		vd2.getPos(key)[2] = vd.getPos(key)[2];

		// Fill some properties randomly

		vd.getProp<0>(key) = 0;
		vd.getProp<1>(key) = 0;
		vd.getProp<2>(key) = key.getKey() + start;

		vd2.getProp<0>(key) = 0;
		vd2.getProp<1>(key) = 0;
		vd2.getProp<2>(key) = key.getKey() + start;

		++it;
	}

	vd.map();
	vd2.map();

	// sync the ghost
	vd.ghost_get<0,2>();
	vd2.ghost_get<0,2>();

	auto NN = vd.getVerlet(r_cut);
	auto p_it = vd.getDomainIterator();

	while (p_it.isNext())
	{
		auto p = p_it.get();

		Point<3,float> xp = vd.getPos(p);

		auto Np = NN.getNNIterator(p.getKey());

		while (Np.isNext())
		{
			auto q = Np.get();

			if (p.getKey() == q)
			{
				++Np;
				continue;
			}

			// repulsive

			Point<3,float> xq = vd.getPos(q);
			Point<3,float> f = (xp - xq);

			float distance = f.norm();

			// Particle should be inside 2 * r_cut range

			if (distance < r_cut )
			{
				vd.getProp<0>(p)++;
				vd.getProp<3>(p).add();
				vd.getProp<3>(p).last().xq = xq;
				vd.getProp<3>(p).last().id = vd.getProp<2>(q);
			}

			++Np;
		}

		++p_it;
	}

	// We now try symmetric Verlet-list Crs scheme

	auto NN2 = vd2.getVerletCrs(r_cut);

	// Because iterating across particles in the CSR scheme require a Cell-list
	auto p_it2 = vd2.getParticleIteratorCRS(NN2.getInternalCellList());

	while (p_it2.isNext())
	{
		auto p = p_it2.get();

		Point<3,float> xp = vd2.getPos(p);

		auto Np = NN2.getNNIterator<NO_CHECK>(p);

		while (Np.isNext())
		{
			auto q = Np.get();

			if (p == q)
			{
				++Np;
				continue;
			}

			// repulsive

			Point<3,float> xq = vd2.getPos(q);
			Point<3,float> f = (xp - xq);

			float distance = f.norm();

			if (distance < r_cut )
			{
				vd2.getProp<1>(p)++;
				vd2.getProp<1>(q)++;

				vd2.getProp<4>(p).add();
				vd2.getProp<4>(q).add();

				vd2.getProp<4>(p).last().xq = xq;
				vd2.getProp<4>(q).last().xq = xp;
				vd2.getProp<4>(p).last().id = vd2.getProp<2>(q);
				vd2.getProp<4>(q).last().id = vd2.getProp<2>(p);
			}

			++Np;
		}

		++p_it2;
	}

	vd2.ghost_put<add_,1>();
	vd2.ghost_put<merge_,4>();

	auto p_it3 = vd.getDomainIterator();

	bool ret = true;
	while (p_it3.isNext())
	{
		auto p = p_it3.get();

		ret &= vd2.getProp<1>(p) == vd.getProp<0>(p);


		vd.getProp<3>(p).sort();
		vd2.getProp<4>(p).sort();

		ret &= vd.getProp<3>(p).size() == vd2.getProp<4>(p).size();

		for (size_t i = 0 ; i < vd.getProp<3>(p).size() ; i++)
			ret &= vd.getProp<3>(p).get(i).id == vd2.getProp<4>(p).get(i).id;

		if (ret == false)
			break;

		++p_it3;
	}

	BOOST_REQUIRE_EQUAL(ret,true);
}

#endif /* SRC_VECTOR_VECTOR_DIST_CELL_LIST_TESTS_HPP_ */
