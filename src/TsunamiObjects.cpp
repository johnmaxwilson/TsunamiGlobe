// Copyright (c) 2015-2017 John M. Wilson, Kasey W. Schultz
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

#include "TsunamiObjects.h"

#include <cassert>
#include <numeric>

#define assertThrow(COND, ERR_MSG) assert(COND);

// ----------------------------------------------------------------------
// -------------------- Main Functions --------------------------------
// ----------------------------------------------------------------------

// Set the height for all elements equal to the depth of the bathymetry below the center of the square.
// Result is squares with just enough water so that the water sits at sea level.
// TODO: set _tot_volume after loading in simulation state file
void tsunamisquares::World::fillToSeaLevel(void) {
    std::map<UIndex, Square>::iterator     sit;

    _tot_volume = 0;

    for (sit=_squares.begin(); sit!=_squares.end(); ++sit) {
        // Add water if the  altitude of the Square center is below sea level, grow total water volume.
        if (sit->second.xyz()[2] < 0.0) {
            sit->second.set_height(fabs(sit->second.xyz()[2]));
            _tot_volume += sit->second.height()*sit->second.area();
        } else {
            sit->second.set_height(0.0);
        }
        // Also initialize the velocities and accel to zero
        sit->second.set_velocity(Vec<2>(0.0,0.0));
        sit->second.set_accel(Vec<2>(0.0,0.0));
    }
}

bool tsunamisquares::World::checkSimHealth(void) {
	bool isHealthy = true;
    std::map<UIndex, Square>::iterator sit;

	for (sit=_squares.begin(); sit!=_squares.end(); ++sit) {
		if(isnan(sit->second.height())      || isinf(sit->second.height())      || sit->second.height() < 0 ||
		   isnan(sit->second.velocity()[0]) || isinf(sit->second.velocity()[0]) ||
		   isnan(sit->second.velocity()[1]) || isinf(sit->second.velocity()[1]) ||
		   isnan(sit->second.momentum()[0]) || isinf(sit->second.momentum()[0]) ||
		   isnan(sit->second.momentum()[1]) || isinf(sit->second.momentum()[1]) ||
		   isnan(sit->second.accel()[0])    || isinf(sit->second.accel()[0])    ||
		   isnan(sit->second.accel()[1])    || isinf(sit->second.accel()[1])    ){
			printSquare(sit->first);
			SquareIDSet bad_neighbors = sit->second.get_valid_neighbors();
			for(SquareIDSet::iterator nit=bad_neighbors.begin(); nit!= bad_neighbors.end(); nit++){
				printSquare(*nit);
			}
			isHealthy = false;
			break;
		}
    }
	return isHealthy;
}
// Precompute diffusion fractions for each square to its neighbors.
void tsunamisquares::World::computeDiffussionFracts(const double dt, const double D){
	std::map<UIndex, Square>::iterator sit;
	Geodesic geod(EARTH_MEAN_RADIUS, 0);


	// Only used for diffuseSquaresSpherical, which isn't our current method for diffusion.
	//  This model is based on the idea that all square will distribute their water to nearby squares each time step,
	//  but it hasn't been shown to actually work for smoothing purposes.

	for(sit=_squares.begin(); sit!=_squares.end(); sit++){
		double         					   lat2, lon2, this_fraction, vertex_distance;
		Vec<2>         					   current_pos;
		std::multimap<double,UIndex>       distsNneighbors;
		std::multimap<double,UIndex>::const_iterator        dnit;
		point_spheq                        new_bottom_left, new_top_left, new_top_right, new_bottom_right;
		std::map<UIndex, double>           collected_fracts;
		std::map<UIndex, double>::iterator frac_it;

		// Roughly matches the area change calculated in the naive method.
		vertex_distance = sqrt(2*sit->second.area()*(2+D*dt-2*sqrt(1+D*dt)));

		current_pos = sit->second.xy();

		// Each corner will spread out, and that new ring will intersect some number of squares

		//bottom left
		geod.Direct(current_pos[1]-_dlat/2, current_pos[0]-_dlon/2, -135, vertex_distance, lat2, lon2);
		new_bottom_left = point_spheq(lon2, lat2);

		//top left
		geod.Direct(current_pos[1]+_dlat/2, current_pos[0]-_dlon/2, -45, vertex_distance, lat2, lon2);
		new_top_left = point_spheq(lon2, lat2);

		//top right
		geod.Direct(current_pos[1]+_dlat/2, current_pos[0]+_dlon/2, 45, vertex_distance, lat2, lon2);
		new_top_right = point_spheq(lon2, lat2);

		//bottom right
		geod.Direct(current_pos[1]-_dlat/2, current_pos[0]+_dlon/2, 135, vertex_distance, lat2, lon2);
		new_bottom_right = point_spheq(lon2, lat2);

		// Make Ring from new vertices for accurate overlap calc
		point_spheq ring_verts[5] = {new_bottom_left, new_top_left, new_top_right, new_bottom_right, new_bottom_left};
		ring_spheq new_ring;
		bg::assign_points(new_ring, ring_verts);

		// get nearest 25 to find the squares to which we need to diffuse
		distsNneighbors = getNearest_rtree(current_pos, 25, false);

		// Find overlap percentage
		double fraction_sum=0;
		for (dnit=distsNneighbors.begin(); dnit!=distsNneighbors.end(); ++dnit) {
			std::vector<poly_spheq> output;

			bg::intersection(new_ring, square(dnit->second).box(), output);

			if(output.size()!=0){
				BOOST_FOREACH(poly_spheq const& p, output)
				{
					this_fraction = bg::area(p)/bg::area(new_ring);;
				}
				collected_fracts.insert(std::make_pair(dnit->second, this_fraction));
				fraction_sum += this_fraction;
			}
		}

		// If any water diffused to areas with no squares, put it back in the source square.
		if(fraction_sum < 1){
			collected_fracts[sit->first] += 1-fraction_sum;
		}else if(fraction_sum > 1){
			for(frac_it=collected_fracts.begin(); frac_it!=collected_fracts.end(); frac_it++){
				frac_it->second = frac_it->second/fraction_sum;
			}
		}

		sit->second.set_diffusion_fractions(collected_fracts);
	}



}

// Diffusion: Remove a volume of water from each square and distribute it to the neighbors.
void tsunamisquares::World::diffuseSquaresSpherical(void) {
    std::map<UIndex, Square>::iterator       sit;
    double                                   add_height;
    Vec<2>                                   add_momentum;
    
	//  This model is based on the idea that all square will spread out their water to nearby squares each time step,
	//  but it hasn't been shown to actually work for smoothing purposes.

    // Initialize updated_heights and momenta, will use this to store the net height and momentum changes
    for (sit=_squares.begin(); sit!=_squares.end(); ++sit) {
        sit->second.set_updated_height(0.0);
        Vec<2> m; m[0] = m[1] = 0.0;
        sit->second.set_updated_momentum(m);
    }

	// Loop through squares
	for (sit=_squares.begin(); sit!=_squares.end(); ++sit) {
		if(sit->second.height() > 0){
			std::map<UIndex, double>			     diffusion_fracts;
			std::map<UIndex, double>::const_iterator dfit;

			diffusion_fracts = sit->second.diffusion_fractions();

			for(dfit = diffusion_fracts.begin(); dfit != diffusion_fracts.end(); dfit++){

				add_height = sit->second.height() * dfit->second;
				square(dfit->first).set_updated_height(square(dfit->first).updated_height() + add_height);

				add_momentum = sit->second.momentum() * dfit->second;
				square(dfit->first).set_updated_momentum(square(dfit->first).updated_momentum() + add_momentum);
			}
		}
	}

    // Set the heights and velocities based on the changes
    for (sit=_squares.begin(); sit!=_squares.end(); ++sit) {
        sit->second.set_height( sit->second.updated_height() );
        if(sit->second.height() > SMALL_HEIGHT){
        	sit->second.set_velocity( sit->second.updated_momentum() / sit->second.mass());
        }else{
        	sit->second.set_velocity(Vec<2>(0.0,0.0));
        	sit->second.set_height(0);
        }
    }
}


//TODO: Also smooth velocity (conserving momentum) after height smoothing
// Diffusion: Remove a volume of water from each square and distribute it to the neighbors.
// Model: area_change = diff_const*time_step
void tsunamisquares::World::diffuseSquaresSchultz(const double dt) {
    std::map<UIndex, Square>::iterator  sit;
    SquareIDSet                         neighborIDs;
    std::map<UIndex, Square>::iterator  nit;
    double                              volume_change, new_level, add_height, height_change;
    Vec<2>                              momentum_change;
    SquareIDSet::iterator               id_it;
    float D = 140616.45;//100000;

    // Initialize updated_heights and momenta, will use this to store the net height and momentum changes
    for (sit=_squares.begin(); sit!=_squares.end(); ++sit) {
        sit->second.set_updated_height( sit->second.height() );
        sit->second.set_updated_momentum( sit->second.momentum() );
    }

    // Compute the height changes due to diffusion of water to neighbors
    for (sit=_squares.begin(); sit!=_squares.end(); ++sit) {
        if (sit->second.height() > 0 && squareLevel(sit->first) != 0.0) {
            // Compute the new height after diffusing the water by 1 time step
            new_level = squareLevel(sit->first)/(1 + D*dt/sit->second.area());
            volume_change = (sit->second.area())*(squareLevel(sit->first) - new_level);
            height_change = new_level - squareLevel(sit->first);
            // Transfer the proportional amount of momentum
            momentum_change = (sit->second.momentum())*volume_change/(sit->second.volume());


			// Divide up the diffused volume equally amongst valid wet neighbors
            neighborIDs = sit->second.get_valid_nearest_neighbors();
            int numInvalidDryNeighbors = 4;
            for (id_it=neighborIDs.begin(); id_it!=neighborIDs.end(); ++id_it) {
				nit = _squares.find(*id_it);
				if(nit->second.height() > 0){
					add_height = volume_change/( nit->second.area()*4.0);
					nit->second.set_updated_height( nit->second.updated_height() + add_height);
					nit->second.set_updated_momentum( nit->second.updated_momentum() + momentum_change/4.0);
					numInvalidDryNeighbors--;
				}
			}
            // For continuity, must self-add 1/4 of the volume change to squares with an invalid neighbor
			height_change += numInvalidDryNeighbors*volume_change/( sit->second.area()*4.0);
            // Add the height change to the updated height
            sit->second.set_updated_height(sit->second.updated_height() + height_change);


            // For continuity, must self-add 1/4 of the volume change to edges and 1/2 to corners.
            // This also balances the momentum distribution.
            /*int minLat = squareLatLon(sit->first)[0] == min_lat();
            int maxLat = squareLatLon(sit->first)[0] == max_lat();
            int minLon = squareLatLon(sit->first)[1] == min_lon();
            int maxLon = squareLatLon(sit->first)[1] == max_lon();
            int cornerSum = minLat + minLon + maxLon + maxLat;
            if (cornerSum == 1) {
                height_change += volume_change/( sit->second.area()*4.0);
            } else if (cornerSum == 2) {
                height_change += volume_change/( sit->second.area()*2.0);
            }*/
            //std::vector<bool>::const_iterator invit;

        }
    }

    //Applies changes and repairs over-diffused squares
    applyDiffusion();
}

// Based on Ward's pair-wise smoothing method.  Our model has non-uniform square sizes, so we have to keep track of volume in a way he doesn't.
void tsunamisquares::World::diffuseSquaresWard(const int ndiffuses) {


    for(int k=0; k<ndiffuses; k++){

	#pragma omp parallel
	{
		std::map<UIndex, Square>::iterator  lsit;
		SquareIDSet                         neighborIDs;
		std::map<UIndex, Square>::iterator  nit;
		double                              volume_change, new_level, add_height, height_change;
		double								fact, diff;
		Vec<2>                              momentum_change;
		Vec<2>								dmom, dvel;
		SquareIDSet::iterator               id_it;


		std::map<UIndex, double> local_height_changes;
		std::map<UIndex, Vec<2> > local_momentum_changes;
		for (UIndex i = 0; i < _squares.size(); i++){
			local_height_changes[i] = 0.0;
			local_momentum_changes[i] = Vec<2>(0.0, 0.0);
		}


	#pragma omp for
		for (UIndex i = 0; i < _squares.size(); i++){
			lsit = _squares.find(i);

    	    // Initialize updated_heights and momenta, will use this to store the net height and momentum changes
			lsit->second.set_updated_height( lsit->second.height() );
			lsit->second.set_updated_momentum( lsit->second.momentum() );
			// Only diffuse if there's water present
			if (lsit->second.height() > SMALL_HEIGHT) {
				// Compute the height-dependent factor for this square, constants taken from Ward
				fact = 0.15*fmin(0.02+0.125*(lsit->second.height()/6000), 0.5);  //Ward: depth dependent smoothing might have to adjust
																//max_depth()
				//simulate multiple applications of smoothing sweeps, instead of actually looping multiple times
				//fact = (1-pow(1-fact, ndiffuses));

				// Go through neighbors to check amount to be exchanged
				neighborIDs = lsit->second.get_valid_nearest_neighbors();
				for (id_it=neighborIDs.begin(); id_it!=neighborIDs.end(); ++id_it) {
					nit = _squares.find(*id_it);

					if(nit->second.height() > SMALL_HEIGHT){
						//if(fmin(lsit->second.height(), nit->second.height()) >= 200){
							//conserve momentum
							dmom = (nit->second.momentum() - lsit->second.momentum())*fact;
							local_momentum_changes[nit->first] -= dmom;
							local_momentum_changes[lsit->first] += dmom;
						/*}else{//TODO: smooth velocities without using momentum as a proxy for shallow water
							//  Smoothing velocities directly for shallow water
							//  Of course, we actually update momenta, so we need to translate through that
							//  *doesn't conserve momentum
							dvel = (nit->second.velocity() - lsit->second.velocity())*fact;

							//Non-omp verion nit->second.set_updated_momentum(nit->second.velocity() - dvel);
							//Non-omp version lsit->second.set_updated_momentum(lsit->second.velocity() + dvel);

							// These masses are from before the diffusion step, so the momentum transfered here will result in velocity changes that aren't
							// quite right.  Let's try it, if it proves pathological we'll readdress it.
							local_momentum_changes[nit->first] -= dvel*nit->second.mass();
							local_momentum_changes[lsit->first] += dvel*lsit->second.mass();
						}*/

						height_change = fact*(squareLevel(nit->first) - squareLevel(lsit->first));
						// Make sure there's water enough to give (or take) as calculated.
						if(height_change >= 0){
							if(height_change < nit->second.height()){
								volume_change = height_change * nit->second.area();
							}else{
								volume_change = nit->second.volume();
							}
						}
						if(height_change < 0){
							if(-height_change < lsit->second.height()){
								volume_change = height_change*lsit->second.area();
							}else{
								volume_change = -lsit->second.volume();
							}
						}

						local_height_changes[nit->first] -= volume_change/nit->second.area();
						local_height_changes[lsit->first] += volume_change/lsit->second.area();
					}//end if neighbor has water
				}//end loop over neighbors
			}//end if this square has water
		}//end parallel for loop over squares

	#pragma omp critical
		{
			// Here is where we combine all local updated heights and momenta into the global updated values
			for (lsit=_squares.begin(); lsit!=_squares.end(); ++lsit) {
				lsit->second.set_updated_height(lsit->second.updated_height()      + local_height_changes[lsit->first]);
				lsit->second.set_updated_momentum( lsit->second.updated_momentum() + local_momentum_changes[lsit->first]);
			}
		}

	}//end parallel block

		//Applies changes and repairs over-diffused squares
		applyDiffusion();

	}//end loop over diffusion sweeps
}

void tsunamisquares::World::applyDiffusion(void){
	// Reset the heights and velocities based on the changes, also catch any heights being set to less than 0 and correct.
	//  we accumulate all this water that never should've been diffused in the first place so everything can be renormalized to conserve mass.
	//  TODO: This method of correcting for over-diffusing water doesn't strictly conserve momentum, and doesn't conserve volume locally (only globally)
	double overdrawn_volume = 0.0;
	std::map<UIndex, Square>::iterator  sit;
	for (sit=_squares.begin(); sit!=_squares.end(); ++sit) {
		if( sit->second.updated_height() < SMALL_HEIGHT){
			overdrawn_volume += sit->second.updated_height()*sit->second.area();
			sit->second.set_updated_height(0.0);
		}

		sit->second.set_height( sit->second.updated_height() );

		if(sit->second.height() > SMALL_HEIGHT){
			sit->second.set_velocity( sit->second.updated_momentum() / sit->second.mass());
		}else{
			sit->second.set_velocity(Vec<2>(0.0,0.0));
			sit->second.set_height(0.0);
		}

	}
	overdrawn_volume = fabs(overdrawn_volume);
	// Scale all the water to conserve mass after we fixed negative volumes
	for (sit=_squares.begin(); sit!=_squares.end(); ++sit) {
		sit->second.set_height( sit->second.height()*total_volume()/(total_volume() - overdrawn_volume) );
	}
}

// Move the water from a Square given its current velocity and acceleration.
// Partition the volume and momentum into the neighboring Squares.
void tsunamisquares::World::moveSquares(const double dt, const bool accel_bool, const bool doPlaneFit, const bool absorbing_boundaries) {

    // Initialize the updated height and velocity to zero. These are the containers
    // used to keep track of the distributed height/velocity from moving squares.

    #pragma omp parallel
    {
    	int thread_id = omp_get_thread_num();
    	int num_threads = omp_get_num_threads();
    	std::map<UIndex, Square>::iterator lsit;
        Geodesic geod(EARTH_MEAN_RADIUS, 0);
		double ldt;
		bool   laccel_bool;
		bool   ldoPlaneFit;

	#pragma omp critical
		{
			ldt          = dt;
			laccel_bool  = accel_bool;
			ldoPlaneFit  = doPlaneFit;
		}

		std::map<UIndex, double> local_updated_heights;
		std::map<UIndex, Vec<2> > local_updated_momenta;
		for (UIndex i = 0; i < _squares.size(); i++){
			local_updated_heights[i] = 0.0;
			local_updated_momenta[i] = Vec<2>(0.0, 0.0);
		}


	#pragma omp for
		for (UIndex i = 0; i < _squares.size(); i++){
			lsit = _squares.find(i);
			lsit->second.set_updated_height(0.0);
			lsit->second.set_updated_momentum(Vec<2>(0.0,0.0));
			// Set acceleration based on the current slope of the water surface
			if(absorbing_boundaries && lsit->second.edge_status()==0){
				updateAcceleration(lsit->first, ldoPlaneFit);
			}else{
				updateAcceleration(lsit->first, ldoPlaneFit);
			}


			Vec<2> current_velo, current_accel, current_pos, new_velo, average_velo, new_center;
			Vec<2> new_bleft, new_tright;
			double local_azimuth, distance_traveled, av_velo_mag, lat2, lon2, a12;
			std::multimap<double, UIndex> distsNneighbors;
			std::map<double, UIndex>::const_iterator dnit;
			point_spheq new_bottom_left, new_bottom_right, new_top_left, new_top_right;

			current_pos = squareCenter(lsit->first);
			current_velo = lsit->second.velocity();

			if(laccel_bool && (!absorbing_boundaries || (absorbing_boundaries && lsit->second.edge_status()==0))){
				current_accel = lsit->second.accel();
			}else{
				current_accel = Vec<2>(0,0);
			}

			// Move the square: calculate average velocity during motion, find azimuth of that vector, and move
			//  each vertex of the square to it's new location on the sphere.  This forms a Ring, which intersects some number of
			//  boxes in our rtree.
			new_velo = current_velo + current_accel*ldt;

			//Catch velocities that run away due to bad local momentum conservation, etc.
			if(new_velo.mag()>sqrt(fabs(G*max_depth()))){
				new_velo = new_velo*sqrt(fabs(G*max_depth()))/new_velo.mag();
			}


			average_velo = current_velo + current_accel*0.5*ldt;
			distance_traveled = average_velo.mag()*ldt;

			//If the square didn't move, or has run out of water, immediately distribute it's water back to itself
			//  (along with any contributions from other squares)
	        if(average_velo == Vec<2>(0.0, 0.0) || lsit->second.height() <= SMALL_HEIGHT){

	        	local_updated_heights[lsit->first] += lsit->second.height();
				local_updated_momenta[lsit->first] += lsit->second.momentum();
	        }else{
				// Calculate azimuth for geodesic calculation
				if(atan2(average_velo[1], average_velo[0]) >= -(M_PI/2)){
					local_azimuth = 90-atan2(average_velo[1], average_velo[0])*(180/M_PI);
				}else{
					local_azimuth = -270-atan2(average_velo[1], average_velo[0])*(180/M_PI);
				}

				//bottom left
				geod.Direct(current_pos[1]-_dlat/2, current_pos[0]-_dlon/2, local_azimuth, distance_traveled, lat2, lon2);
				//new_bottom_left = point_spheq(lon2, lat2);
				new_bleft = Vec<2>(lon2, lat2);

				//top left
				//geod.Direct(current_pos[1]+_dlat/2, current_pos[0]-_dlon/2, local_azimuth, distance_traveled, lat2, lon2);
				//new_top_left = point_spheq(lon2, lat2);

				//top right
				geod.Direct(current_pos[1]+_dlat/2, current_pos[0]+_dlon/2, local_azimuth, distance_traveled, lat2, lon2);
				//new_top_right = point_spheq(lon2, lat2);
				new_tright = Vec<2>(lon2, lat2);

				//bottom right
				//geod.Direct(current_pos[1]-_dlat/2, current_pos[0]+_dlon/2, local_azimuth, distance_traveled, lat2, lon2);
				//new_bottom_right = point_spheq(lon2, lat2);

				//center, for nearest neighbor retrieval
				//geod.Direct(current_pos[1], current_pos[0], local_azimuth, distance_traveled, lat2, lon2);
				//new_center = Vec<2>(lon2, lat2);

				// Do a quick grab of nearest squares to new location, then do the accurate intersection check later
				// TODO: if this is added in again, put rtree access in an omp critical block
				//distsNneighbors = getNearest_rtree(new_center, lnum_nearest, false);
				SquareIDSet neighbor_ids = lsit->second.get_valid_neighbors();
				distsNneighbors.insert(std::make_pair(0.0, lsit->first));
				for(SquareIDSet::iterator sidit = neighbor_ids.begin(); sidit != neighbor_ids.end(); sidit++){
					distsNneighbors.insert(std::make_pair(1.0, *sidit));
				}


				// Init these for renormalizing the fractions
				double this_fraction;
				double fraction_sum = 0.0;
				std::map<UIndex, double> originalFractions, renormFractions;
				std::map<UIndex, double>::iterator frac_it;
				std::map<UIndex, double> x_fracmap, y_fracmap;


				/*// Make Ring from new vertices for accurate overlap calc
				point_spheq ring_verts[5] = {new_bottom_left, new_top_left, new_top_right, new_bottom_right, new_bottom_left};
				ring_spheq new_ring;
				bg::assign_points(new_ring, ring_verts);*/

				// Iterate through the neighbors and compute overlap area between ring and neighbor boxes
				bool is_any_overlap = false;
				for (dnit=distsNneighbors.begin(); dnit!=distsNneighbors.end(); ++dnit) {
					std::map<UIndex, Square>::iterator neighbor_it = _squares.find(dnit->second);
					std::vector<poly_spheq> output;
					double overlap_area=0.0;

					overlap_area = box_overlap_area(new_bleft, new_tright, neighbor_it->second.box(), geod);
					this_fraction = overlap_area/lsit->second.area() - max_overlap_error();

					if(this_fraction > 0){
						is_any_overlap = true;
						fraction_sum += this_fraction;
						originalFractions.insert(std::make_pair(neighbor_it->first, this_fraction));
					}

					// (Allegedly) more accurate overlap calculation using the boost library; v1.64 gives incorrect overlaps
					/*bg::intersection(new_ring, neighbor_it->second.box(), output);

					if(output.size() != 0){
						is_any_overlap = true;

						BOOST_FOREACH(poly_spheq p, output)
						{
							bg::unique(p);

							//overlap_area = bg::area(p)*EARTH_MEAN_RADIUS*EARTH_MEAN_RADIUS;

							PolygonArea geo_poly(geod);
							bg::for_each_point(p, make_geo_poly<point_spheq>(geo_poly));
							double perimeter;
							unsigned n = geo_poly.Compute(true, true, perimeter, overlap_area);

							this_fraction = overlap_area/sit->second.area();
							std::cout << sit->first << "x" << dnit->second << "; frac=" << std::fixed << this_fraction << "; overlap=" << overlap_area << "; sq area=" << sit->second.area() <<std::endl;
							if(sit->first != dnit->second && this_fraction>0.9){
								//std::cout << std::endl<<sit->first << "x" << dnit->second << "; frac=" << std::fixed << this_fraction << "; overlap=" << overlap_area << "; sq area=" << sit->second.area() <<std::endl;

								std::cout << "\t square:   " << bg::dsv(sit->second.polygon()) << std::endl;
								std::cout << "\t newring:  " << bg::dsv(new_ring) << std::endl;
								std::cout << "\t overlap:  " << bg::dsv(p) << std::endl;
								std::cout << "\t neighbor: " << bg::dsv(square(dnit->second).polygon()) << std::endl;
							}
						}
						fraction_sum += this_fraction;
						originalFractions.insert(std::make_pair(neighbor_it->first, this_fraction));
					}*/

				}

				// If no overlap, we'll just distribute water to nearest square
				if(!is_any_overlap){
					originalFractions.insert(std::make_pair(distsNneighbors.begin()->second, 1.0));
					fraction_sum = 1.0;
				}

				// Then normalize these fractions to enforce conservation.

				for (frac_it=originalFractions.begin(); frac_it!=originalFractions.end(); ++frac_it) {
					//assertThrow((frac_it->second)/fraction_sum <= 1, "Area fraction must be less than 1.");
					renormFractions.insert(std::make_pair(frac_it->first, (frac_it->second)/fraction_sum));
				}

				// Compute height and momentum imparted to neighbors
				for (frac_it=renormFractions.begin(); frac_it!=renormFractions.end(); ++frac_it) {
					// This iterator will give us the neighbor square
					std::map<UIndex, Square>::iterator neighbor_it = _squares.find(frac_it->first);
					//// This iterates through the renormalized fractions
					//frac_it = renormFractions.find(nit->second);
					double areaFraction = frac_it->second;

					// Update the amount of water in the neighboring square (conserve volume)
					double dV = lsit->second.volume()*areaFraction;
					double H = local_updated_heights[neighbor_it->first];//neighbor_it->second.updated_height();
					double A_n = neighbor_it->second.area();

					local_updated_heights[neighbor_it->first] = H + dV/A_n;

					// Conserve momentum, update the velocity accordingly (at the end)
					Vec<2> dM = new_velo*lsit->second.mass()*areaFraction;
					Vec<2> M  = local_updated_momenta[neighbor_it->first];

					local_updated_momenta[neighbor_it->first] = M + dM;

				} //end loop setting updated heights and momenta from this square
	        }//end else statement for moving squares or not
		}//end omp for loop over squares


	#pragma omp critical
		{
			// Here is where we combine all local updated heights and momenta into the global updated values
		    for (lsit=_squares.begin(); lsit!=_squares.end(); ++lsit) {
				lsit->second.set_updated_height(lsit->second.updated_height()+local_updated_heights[lsit->first]);
				lsit->second.set_updated_momentum( lsit->second.updated_momentum() + local_updated_momenta[lsit->first]);
			}
		}

	}//end parallel block

    // Loop again over squares to set new velocity and height from accumulated height and momentum
    std::map<UIndex, Square>::iterator sit;
    for (sit=_squares.begin(); sit!=_squares.end(); ++sit) {

        sit->second.set_height(sit->second.updated_height());

        Vec<2> veltoset = sit->second.updated_momentum()/(sit->second.mass());


        // Here we're trying to catch bad velocities if they arrise.
        if(isnan(veltoset[0]) || isinf(veltoset[0]) || isnan(veltoset[1]) || isinf(veltoset[1])){
        	veltoset = Vec<2>(0, 0);
        }

        // Kill velocity on edge squares
        if(absorbing_boundaries && sit->second.edge_status()==2){
        	sit->second.set_velocity( Vec<2>(0.0, 0.0));
        }

        // Don't set any velcity or height to tiny amounts of water
        if(sit->second.height() > SMALL_HEIGHT){
        	sit->second.set_velocity( veltoset );
        }else{
            sit->second.set_velocity( Vec<2>(0.0, 0.0));
            sit->second.set_height(0);
        }

    }
    
}

void tsunamisquares::World::updateAcceleration(const UIndex &square_id, const bool doPlaneFit) {
    std::map<UIndex, Square>::iterator square_it = _squares.find(square_id);
    Vec<2> grav_accel, friction_accel, new_accel, gradient;
    
    // Only accelerate the water in this square IF there is water in this square
    if (square_it->second.height() > SMALL_HEIGHT) {
        // gravitational acceleration due to the slope of the water surface
        gradient = getGradient(square_id, doPlaneFit);
        
        grav_accel = gradient*G*(-1.0);

        // Hard cutoff limit to 1 G of accel
        if(isnan(grav_accel[0]) || isinf(grav_accel[0])){
        	grav_accel[0] = 0.0;
        }
     	if(isnan(grav_accel[1]) || isinf(grav_accel[1]) ){
        	grav_accel[1] = 0.0;
        }
        if(grav_accel.mag() > G) {
			grav_accel *= (G/(long double)grav_accel.mag());
		}
        /*// limit to 0.5g, as per Ward
        //  with this process, acceleration is maximized for a slope of 1.  Extreme slopes won't move super fast,
        //  they'll tend to sit in one place while several smoothing iterations take their effect.
        //  This currently produces pathological behavior.
        double ang = -atan(gradient[0]);
        grav_accel[0] = G*sin(ang)*cos(ang);

        ang = -atan(gradient[1]);
        grav_accel[1] = G*sin(ang)*cos(ang);*/

        // frictional acceleration from fluid particle interaction
        friction_accel = square_it->second.velocity()*(-1.0)*square_it->second.velocity().mag()*(square_it->second.friction())/(fmax(square_it->second.height(), 1.0));
        //friction_accel = Vec<2>(0.0,0.0);

        if(isnan(friction_accel[0]) || isinf(friction_accel[0])){
        	friction_accel[0] = 0.0;
        }
     	if(isnan(friction_accel[1]) || isinf(friction_accel[1]) ){
     		friction_accel[1] = 0.0;
        }

        new_accel = grav_accel + friction_accel;

        // Check for invalid directions of motion (eg at edges)
        if(square_it->second.invalid_directions()[0] && new_accel[0]<0.0) new_accel[0] = 0.0;
        if(square_it->second.invalid_directions()[1] && new_accel[0]>0.0) new_accel[0] = 0.0;
        if(square_it->second.invalid_directions()[2] && new_accel[1]<0.0) new_accel[1] = 0.0;
        if(square_it->second.invalid_directions()[3] && new_accel[1]>0.0) new_accel[1] = 0.0;

        // Set the acceleration
        square_it->second.set_accel(new_accel);
    } else {
        square_it->second.set_accel( Vec<2>(0.0, 0.0) );
    }
}


tsunamisquares::Vec<2> tsunamisquares::World::getGradient(const UIndex &square_id, const bool doPlaneFit) {
    std::map<UIndex, Square>::const_iterator square_it = _squares.find(square_id);
    Vec<2> gradient;
    bool debug = false;
    SquareIDSet square_ids_to_fit;


	square_ids_to_fit = get_neighbors_for_accel(square_id);
	// Handling of special cases for points all in a line (1, 2, or 3) handled in below functions
    if(doPlaneFit){
    	gradient = fitPointsToPlane(square_id, square_ids_to_fit);
    } else {
    	gradient = getAverageSlopeWard(square_id, square_ids_to_fit);
    }

    return gradient;
}


tsunamisquares::SquareIDSet tsunamisquares::World::get_neighbors_for_accel(const UIndex &square_id) const {
    SquareIDSet                 	      valid_squares, all_neighbors_and_self;
    SquareIDSet::const_iterator       	                                  id_it;
    bool									             condition1, condition2;

    // Grab all valid neighbors
    all_neighbors_and_self = const_square(square_id).get_neighbors_and_self();

    // Only include the neighbors if they are not "hi and dry".
    // A wave incident on the beach is not pushed backwards by the tall beach in front of it.
    // The wave only falls back into the ocean after it has water below it that
    // defines a slope for the water surface.
    
    for (id_it=all_neighbors_and_self.begin(); id_it!=all_neighbors_and_self.end(); ++id_it) {
		// If dry and taller than this square, do not include.  If dry and lower, do include
		//        e.g. Are they dry, what's their level, height, etc.

    	condition1 = const_square(*id_it).height() >= 0;
    	condition2 = const_square(*id_it).xyz()[2] < squareLevel(square_id);
		if(condition2 && condition1){
			valid_squares.insert(*id_it);
		}
    }
    
    return valid_squares;
}


tsunamisquares::Vec<2> tsunamisquares::World::fitPointsToPlane(const UIndex &this_id, const SquareIDSet &square_ids) {
    // --------------------------------------------------------------------
    // Based on StackOverflow article:
    // http://stackoverflow.com/questions/1400213/3d-least-squares-plane
	// solving Ax = b  where x gives values of plane z(x, y) = x1*x + x2*y + x3
    // --------------------------------------------------------------------
	int								N = square_ids.size();

    std::vector<double>             x_vals, y_vals, z_vals;
    x_vals.reserve(N);
    y_vals.reserve(N);
    z_vals.reserve(N);

    double 							xav, yav, zav, xvar, yvar;
    SquareIDSet::const_iterator     id_it;
    Vec<2>                          gradient;
    SquareIDSet::const_iterator     iit;
    std::map<UIndex, Vec<2> >       neighborsAndCoords, neighbors_for_fitting;
    int                             i;
    Vec<9>                          A;
    Vec<3>                          b, x;
    
	neighborsAndCoords = square(this_id).local_neighbor_coords();

	// loop over the provided valid neighbor ids, get their relative positions from neighborsAndCoords
    for (id_it=square_ids.begin(); id_it!=square_ids.end(); ++id_it) {
        x_vals.push_back(neighborsAndCoords[*id_it][0]);
        y_vals.push_back(neighborsAndCoords[*id_it][1]);
        z_vals.push_back(squareLevel(*id_it));
    }
    xav = std::accumulate( x_vals.begin(), x_vals.end(), 0.0)/x_vals.size();
    yav = std::accumulate( y_vals.begin(), y_vals.end(), 0.0)/y_vals.size();
    zav = std::accumulate( z_vals.begin(), z_vals.end(), 0.0)/z_vals.size();

    // Build the b vector and the A matrix.
    // Single index for matrix, array style. A[i][j] = A_vec[i*3 + j],  N_cols=3
    for (i=0; i<N; ++i) {
            
        b[0] += (x_vals[i]-xav)*(z_vals[i]-zav);
        b[1] += (y_vals[i]-yav)*(z_vals[i]-zav);
        b[2] += (z_vals[i]-zav);
        
        A[0] += (x_vals[i]-xav)*(x_vals[i]-xav);
        A[1] += (x_vals[i]-xav)*(y_vals[i]-yav);
        A[2] += (x_vals[i]-xav);
        A[3] += (x_vals[i]-xav)*(y_vals[i]-yav);
        A[4] += (y_vals[i]-yav)*(y_vals[i]-yav);
        A[5] += (y_vals[i]-yav);
        A[6] += (x_vals[i]-xav);
        A[7] += (y_vals[i]-yav);
        A[8] += 1.0;
    }

    //Cramer's rule for 3x3 system
    double det_A = (A[0]*A[4]*A[8]+A[1]*A[5]*A[6]+A[3]*A[7]*A[2]-A[2]*A[4]*A[6]-A[1]*A[3]*A[8]-A[0]*A[5]*A[7]);

    //Deal with det(A)=0, eg points all in a line
    if(det_A > CROSS_TOLERANCE){
    	gradient[0] = (b[0]*A[4]*A[8]+A[1]*A[5]*b[2]+b[1]*A[7]*A[2]-A[2]*A[4]*b[2]-A[1]*b[1]*A[8]-b[0]*A[5]*A[7])/det_A;
    	gradient[1] = (A[0]*b[1]*A[8]+b[0]*A[5]*A[6]+A[3]*b[2]*A[2]-A[2]*b[1]*A[6]-b[0]*A[3]*A[8]-A[0]*A[5]*b[2])/det_A;
    }else{
    	if(N < 2){
    		// Only 1 or 0 points in the fit
    		gradient = Vec<2>(0.0,0.0);
    	} else{
    		// Two or more points in a line, do a least squares line in each direction, set any invalid distances to 0
    		gradient[0] = gradient[1] = 0;
    		xvar = yvar = 0;

    		for(i=0; i<N; i++){
    			gradient[0] += (x_vals[i]-xav)*(z_vals[i]-zav);
    			xvar += (x_vals[i]-xav)*(x_vals[i]-xav);

    			gradient[1] += (y_vals[i]-yav)*(z_vals[i]-zav);
				yvar += (y_vals[i]-yav)*(y_vals[i]-yav);
    		}

    		gradient[0] = gradient[0]/xvar;
    		gradient[1] = gradient[1]/yvar;
    		if(isnan(gradient[0]) || isinf(gradient[0]))  gradient[0]=0.0;
    		if(isnan(gradient[1]) || isinf(gradient[1]))  gradient[1]=0.0;
    	}
    }

    return gradient;

    // Matrix determinant debugging
    /*if(det_A == 0.0){
     	std::cout<<"\n 0 det(A) for ID "<< this_id << std::endl;
        //std::cout << "\npre x: " << x[0] << ", " << x[1] << ", " << x[2] << std::endl;
        //std::cout << "\n\nb: " << b[0] << ", " << b[1] << ", " << b[2] << std::endl;
        std::cout << std::fixed << "A: " << A[0] << ",\t " << A[1] << ",\t " << A[2] << std::endl;
        std::cout << std::fixed << "   " << A[3] << ",\t " << A[4] << ",\t " << A[5] << std::endl;
        std::cout << std::fixed << "   " << A[6] << ",\t " << A[7] << ",\t " << A[8] << std::endl;

        std::cout <<"\nxs: "<<std::endl;
        for(std::vector<double>::iterator i=x_vals.begin(); i!=x_vals.end(); i++){
        	std::cout << std::fixed <<"    "<< *i << std::endl;
        }

        std::cout <<"\nys: "<<std::endl;
        for(std::vector<double>::iterator i=y_vals.begin(); i!=y_vals.end(); i++){
        	std::cout << std::fixed <<"    "<< *i << std::endl;
        }
    }*/
    
    // Matrix solver below is adapted from Virtual Quake
    /*
    int     j, k;
    double  v, f, sum;
    int     n = 3;
    for (i=0; i<n; ++i) {
        v = A[i+n*i];
        for (j=i+1; j<n; ++j) {
            f = A[i+n*j]/v;
            for (k=0; k<n; ++k) {
                A[k+n*j] -= f*A[k+n*i];
            }
            b[j] -= f*b[i];
        }
    }
    for (i=n-1; i>=0; --i) {
        sum = b[i];
        for (j=i+1; j<n; ++j) {
            sum -= A[j+n*i]*x[j];
        }
        x[i] = sum/A[i+n*i];
    }
    */
}


tsunamisquares::Vec<2> tsunamisquares::World::getAverageSlopeWard(const UIndex &square_id, const SquareIDSet &square_ids) const {
    std::map<UIndex, Square>::const_iterator square_it = _squares.find(square_id);
    SquareIDSet::const_iterator     id_it;
    std::map<UIndex, Vec<2> > neighborsAndCoords;
    Vec<2> gradient;
    bool debug = false;
    
	neighborsAndCoords = square_it->second.local_neighbor_coords();

    int num_horiz  = 0;
    int num_vert   = 0;
    gradient[0]    = 0;
    gradient[1]    = 0;
    for (id_it=square_ids.begin(); id_it!=square_ids.end(); ++id_it) {

		if(*id_it==square_it->second.left()){
			gradient[0] += (squareLevel(square_it->first)-squareLevel(*id_it))/(-1*neighborsAndCoords[*id_it][0]);
			num_horiz++;
			continue;
		}
		if(*id_it==square_it->second.right()){
			gradient[0] += (squareLevel(*id_it)-squareLevel(square_it->first))/neighborsAndCoords[*id_it][0];
			num_horiz++;
			continue;
		}
		if(*id_it==square_it->second.bottom()){
			gradient[1] += (squareLevel(square_it->first)-squareLevel(*id_it))/(-1*neighborsAndCoords[*id_it][1]);
			num_vert++;
			continue;
		}
		if(*id_it==square_it->second.top()){
			gradient[1] += (squareLevel(*id_it)-squareLevel(square_it->first))/neighborsAndCoords[*id_it][1];
			num_vert++;
			continue;
		}
    }

	if(num_horiz == 0){
		gradient[0] = 0.0;
	} else{
		gradient[0] = gradient[0]/num_horiz;
	}
	if(num_vert == 0){
		gradient[1] = 0.0;
	} else{
		gradient[1] = gradient[1]/num_vert;
	}

	/*Kasey's old code.  Seems to be eqivalent, but I'm handling the different cases external to this function now
	// Initialize the 4 points that will be used to approximate the slopes d/dx and d/dy
    // for this square. These are the centers of the neighbor squares.
    Vec<2> center = squareCenter(square_id);

    UIndex leftID   = square_it->second.left();
    UIndex rightID  = square_it->second.right();
    UIndex topID    = square_it->second.top();
    UIndex bottomID = square_it->second.bottom();

	// Altitude of water level of neighbor squares
	double z_left   = squareLevel(leftID);
	double z_right  = squareLevel(rightID);
	double z_top    = squareLevel(topID);
	double z_bottom = squareLevel(bottomID);
	double z_mid    = squareLevel(square_id);

	// Thickness of water in neighbor squares
	double h_left   = _squares.find(leftID)->second.height();
	double h_right  = _squares.find(rightID)->second.height();
	double h_top    = _squares.find(topID  )->second.height();
	double h_bottom = _squares.find(bottomID)->second.height();
	double h_mid    = square_it->second.height();

	neighborsAndCoords = square_it->second.local_neighbor_coords();

	// X,Y of neighbor squares
	Vec<2> center_L = neighborsAndCoords[leftID];
	Vec<2> center_R = neighborsAndCoords[rightID];
	Vec<2> center_T = neighborsAndCoords[topID];
	Vec<2> center_B = neighborsAndCoords[bottomID];

	// ================================================================
	// Gradient = (dz/dx, dz/dy)
	// Handle the cases with dry cells on either left/right/top/bottom.
	// IGNORE cells that are hi and dry
	// ================================================================
	if (h_left == 0.0 && h_right == 0.0 && h_top == 0.0 && h_bottom == 0.0) {
	// Case: No water on any side
		gradient[0] = 0.0;
		gradient[1] = 0.0;
	} else  {
		if (h_left > 0.0 && h_right > 0.0 && h_top > 0.0 && h_bottom > 0.0) {
		// Case: No dry neighbors, then do normal gradient
			gradient[0] = (z_right-z_left)/( center_L.dist(center_R) );
			gradient[1] = (z_top-z_bottom)/( center_T.dist(center_B) );
		}

		// Case: Hi and dry on the right, water to the left
		if (h_right == 0.0 && z_right >= 0.0 && h_left != 0.0) {
			gradient[0] = (z_mid-z_left)/( center_L.dist(center) );
		} else if (h_left == 0.0 && z_left >= 0.0 && h_right != 0.0) {
		// Case: Hi and dry on the left, water to the right
			gradient[0] = (z_right-z_mid)/( center_R.dist(center) );
		}


		// Case: Hi and dry on the top, water on bottom
		if (h_top == 0.0 && z_top >= 0.0 && h_bottom != 0.0) {
			gradient[1] = (z_mid-z_bottom)/( center.dist(center_B) );
		} else if (h_left == 0.0 && z_left >= 0.0 && h_right != 0.0) {
		// Case: Hi and dry on the bottom, water on top
			gradient[1] = (z_top-z_mid)/( center_T.dist(center) );
		}

	}*/

    
    return gradient;
    
}


// Raise/lower the sea floor depth at the square's vertex by an amount "height_change"
void tsunamisquares::World::deformBottom(const UIndex &square_id, const double &height_change) {
    std::map<UIndex, Square>::iterator sit = _squares.find(square_id);
    LatLonDepth new_lld;
    double old_altitude;
    
    new_lld = sit->second.lld();
    old_altitude = new_lld.altitude();
    new_lld.set_altitude(old_altitude + height_change);
    sit->second.set_lld(new_lld);
}

tsunamisquares::Vec<2> tsunamisquares::World::centralLoc(void){
	LatLonDepth min_bound, max_bound;
	Vec<3> min_xyz, max_xyz;
	double dx, dy;
	get_bounds(min_bound, max_bound);

    min_xyz = Vec<3>(min_bound.lon(), min_bound.lat(), min_bound.altitude());
    max_xyz = Vec<3>(max_bound.lon(), max_bound.lat(), max_bound.altitude());
    dx = max_xyz[0]-min_xyz[0];
    dy = max_xyz[1]-min_xyz[1];

	return Vec<2>(min_xyz[0]+dx/2.0, min_xyz[1]+dy/2.0);
}

void tsunamisquares::World::bumpCenter(const double bump_height) {;

	Vec<2> centerLoc = centralLoc();

	UIndex centralID = getNearest_rtree(centerLoc, 1, false).begin()->second;

	for (int i = 0; i < 2; i ++ ){
		deformBottom(centralID + num_lons()*(i-1),   bump_height/2.0);
		deformBottom(centralID + num_lons()*(i-1)+1, bump_height/2.0);
	}
	for (int i = 0; i < 4; i ++ ){
		//if(i==1 || i==2){deformBottossh(centralID + num_lons()*(i-1)-1 , bump_height/2.0);}
		deformBottom(centralID + num_lons()*(i-2)-1 , bump_height/2.0);
		deformBottom(centralID + num_lons()*(i-2)   , bump_height/2.0);
		deformBottom(centralID + num_lons()*(i-2)+1 , bump_height/2.0);
		deformBottom(centralID + num_lons()*(i-2)+2 , bump_height/2.0);
		//if(i==1 || i==2){deformBottom(centralID + num_lons()*(i-1)+2 , bump_height/2.0);}
	}
}


// Flatten the bottom to be the specified depth
void tsunamisquares::World::flattenBottom(const double &depth) {
    std::map<UIndex, Square>::iterator sit;
    LatLonDepth new_lld;
    double newDepth = -fabs(depth);
    
    // Assign the depth for all vertices to be newDepth
    for (sit=_squares.begin(); sit!=_squares.end(); ++sit) {
        new_lld = sit->second.lld();
        new_lld.set_altitude(newDepth);
        sit->second.set_lld(new_lld);
    }
}

void tsunamisquares::World::gaussianPile(const double hgauss, const double std){
    std::map<UIndex, Square>::iterator sit;
    Vec<2> centerLoc = centralLoc();

    for (sit=_squares.begin(); sit!=_squares.end(); ++sit) {
        double disc = hgauss * exp(-pow((tsunamisquares::boostDistance(centerLoc, sit->second.xy())/std), 2));
        sit->second.set_height(-sit->second.xyz()[2] + disc);
    }
}


//
//// ----------------------------------------------------------------------
//// -------------------- Utility Functions -------------------------------
//// ----------------------------------------------------------------------

// Find maximum depth in simulation
void tsunamisquares::World::calcMaxDepth() {
    std::map<UIndex, Square>::const_iterator sit;
    double maxDepth, thisDepth;
    maxDepth = DBL_MAX;

    for (sit=_squares.begin(); sit!=_squares.end(); ++sit) {
    	thisDepth = sit->second.lld().altitude();
    	if(thisDepth < maxDepth){
        	maxDepth = thisDepth;
        }
    }
    _max_depth = maxDepth;
}

//Find minimum square side length in simulation
void tsunamisquares::World::calcMinSpacing() {
	std::map<UIndex, Square>::const_iterator sit;
	double minSize, thisX, thisY;
	minSize = DBL_MAX;
	Vec<2> center;
	double vert_dist, top_horiz_dist, bottom_horiz_dist;

	for (sit=_squares.begin(); sit!=_squares.end(); ++sit) {
		center = sit->second.xy();

		vert_dist   = bg::distance(point_spheq(center[0], center[1]-_dlat/2), point_spheq(center[0], center[1]+_dlat/2));
		top_horiz_dist    = bg::distance(point_spheq(center[0]-_dlon/2, center[1]+_dlat/2), point_spheq(center[0]+_dlon/2, center[1]+_dlat/2));
		bottom_horiz_dist = bg::distance(point_spheq(center[0]-_dlon/2, center[1]-_dlat/2), point_spheq(center[0]+_dlon/2, center[1]-_dlat/2));

		if(vert_dist < minSize){
			minSize = vert_dist;
		}
		if(top_horiz_dist < minSize){
			minSize = top_horiz_dist;
		}
		if(bottom_horiz_dist < minSize){
			minSize = bottom_horiz_dist;
		}
	}
	//bg::distance returns angular distance in radians
	_min_spacing = minSize*EARTH_MEAN_RADIUS;
}

void tsunamisquares::World::calcMaxOverlapError() {
	std::map<UIndex, Square>::const_iterator sit;
    Geodesic geod(EARTH_MEAN_RADIUS, 0);
    double max_overlap_error = 0;

    std::cout<<".. Calculating maximum error in overlaps: ";

	for (sit=_squares.begin(); sit!=_squares.end(); ++sit) {
		double lat2, lon2;
        Vec<2> new_bleft, new_tright;
        std::multimap<double, UIndex> distsNneighbors;
		std::map<double, UIndex>::const_iterator dnit;

		//bottom left
		geod.Direct(sit->second.xy()[1]-_dlat/2, sit->second.xy()[0]-_dlon/2, 90.0, 0.0, lat2, lon2);
		new_bleft = Vec<2>(lon2, lat2);
		//top right
		geod.Direct(sit->second.xy()[1]+_dlat/2, sit->second.xy()[0]+_dlon/2, 90.0, 0.0, lat2, lon2);
		new_tright = Vec<2>(lon2, lat2);

		distsNneighbors = getNearest_rtree(sit->second.xy(), 9, false);

		for (dnit=distsNneighbors.begin(); dnit!=distsNneighbors.end(); ++dnit) {
			std::map<UIndex, Square>::iterator neighbor_it = _squares.find(dnit->second);
			std::vector<poly_spheq> output;
			double overlap_area=0.0;

			if(dnit->second != sit->first){
			overlap_area = box_overlap_area(new_bleft, new_tright, neighbor_it->second.box(), geod);
			max_overlap_error = std::max(max_overlap_error, overlap_area/sit->second.area());
			}
		}
	}

	std::cout << std::scientific << max_overlap_error<< std::fixed<< "....";
	_max_overlap_error = max_overlap_error;

}

// Much faster n-nearest neighbor search using an RTree
std::multimap<double, tsunamisquares::UIndex> tsunamisquares::World::getNearest_rtree(const Vec<2> &location, const int &numNear, const bool wet_bool)const {
    std::vector<UIndex>						  nIDs;
    std::multimap<double, UIndex>             neighbors;
    double 									  square_dist;

    //Use RTree query to get numNear nearest neighbors
    if(wet_bool){
    	nIDs = _wet_rtree.getNearest(location, numNear);
    }else{
    	nIDs = _square_rtree.getNearest(location, numNear);
    }

    // Compute distance from "location" to the center of each neighbor.
    for (int i=0; i<nIDs.size(); ++i) {
		square_dist = bg::distance(point_spheq(squareCenter(nIDs[i])[0], squareCenter(nIDs[i])[1]),
									point_spheq(location[0], location[1]))*EARTH_MEAN_RADIUS;
		neighbors.insert(std::make_pair(square_dist, nIDs[i]));
	}
    return neighbors;
}

tsunamisquares::SquareIDSet tsunamisquares::World::getRingIntersects_rtree(const ring_spheq &ring)const {
    SquareIDSet	  intersects;
    //Use RTree query to get all boxes that the provided ring intersects (edges and contained within)
    intersects = _square_rtree.getRingIntersects(ring);

    return intersects;
}

tsunamisquares::SquareIDSet tsunamisquares::World::getBoxIntersects_rtree(const box_spheq &box)const {
	SquareIDSet	  intersects;
    //Use RTree query to get all boxes that the provided box intersects (edges and contained within)
    intersects = _square_rtree.getBoxIntersects(box);

    return intersects;
}



// Get the square_id for each closest square to some location = (x,y)
tsunamisquares::UIndex tsunamisquares::World::whichSquare(const Vec<2> &location) const {
    std::map<double, UIndex>                  square_dists;
    std::map<UIndex, Square>::const_iterator  sit;
    UIndex                               neighbor;

    // Compute distance from "location" to the center of each square.
    // Since we use a map, the distances will be ordered since they are the keys
    for (sit=_squares.begin(); sit!=_squares.end(); ++sit) {
        double square_dist = squareCenter(sit->first).dist(location);
        square_dists.insert(std::make_pair(square_dist, sit->second.id()));
    }
    
    // Return the ID of the nearest square
    return square_dists.begin()->second;
}


void tsunamisquares::World::indexNeighbors() {
	computeNeighbors();
	computeNeighborCoords();
	computeEdgeStatus();
	calcMaxOverlapError();
}

void tsunamisquares::World::computeNeighbors(void) {
    std::map<UIndex, Square>::iterator                                  sit;
    double                                              this_lat, this_lon;
    bool                                    isMinLat, isMinLon, isMaxLat, isMaxLon;
    UIndex                       this_id, left, right, top_right, top_left;
    UIndex                          bottom_left, bottom_right, top, bottom;

    // Use the in-place element numbering to find the IDs of the neighboring squares.
    // Must handle the border and corner cases and not include off-model neighbors.

    for (sit=_squares.begin(); sit!=_squares.end(); ++sit) {
        this_id      = sit->first;
        left         = this_id-1;
        right        = this_id+1;
        top          = this_id+num_lons();
        bottom       = this_id-num_lons();
        top_left     = top-1;
        top_right    = top+1;
        bottom_left  = bottom-1;
        bottom_right = bottom+1;

        int this_lon_index = this_id%num_lons();
        int this_lat_index = ((this_id-(this_id%num_lons()))/num_lons())%num_lats();

        isMinLon      = (this_lon_index == 0);
        isMaxLon      = (this_lon_index == num_lons()-1);
        isMinLat      = (this_lat_index == 0);
        isMaxLat      = (this_lat_index == num_lats()-1);

        // Handle the corner and edge cases
        if (!(isMaxLat || isMaxLon || isMinLon || isMinLat)) {
            // Interior squares
            sit->second.set_right(right);
            sit->second.set_left(left);
            sit->second.set_top(top);
            sit->second.set_bottom(bottom);
            sit->second.set_top_left(top_left);
            sit->second.set_top_right(top_right);
            sit->second.set_bottom_left(bottom_left);
            sit->second.set_bottom_right(bottom_right);
        } else if (isMaxLat && isMinLon) {
            // Top left (North West) corner
            sit->second.set_right(right);
            sit->second.set_bottom(bottom);
            sit->second.set_bottom_right(bottom_right);
        } else if (isMaxLat && isMaxLon) {
            // Top right (North East) corner
            sit->second.set_left(left);
            sit->second.set_bottom(bottom);
            sit->second.set_bottom_left(bottom_left);
        } else if (isMinLat && isMaxLon) {
            // Bottom right (South East) corner
            sit->second.set_left(left);
            sit->second.set_top(top);
            sit->second.set_top_left(top_left);
        } else if (isMinLat && isMinLon) {
            // Bottom left (South West) corner
            sit->second.set_right(right);
            sit->second.set_top(top);
            sit->second.set_top_right(top_right);
        } else if (isMinLon) {
            // Left (West) border
            sit->second.set_right(right);
            sit->second.set_top(top);
            sit->second.set_bottom(bottom);
            sit->second.set_top_right(top_right);
            sit->second.set_bottom_right(bottom_right);
        } else if (isMaxLat) {
            // Top (North) border
            sit->second.set_right(right);
            sit->second.set_left(left);
            sit->second.set_bottom(bottom);
            sit->second.set_bottom_left(bottom_left);
            sit->second.set_bottom_right(bottom_right);
        } else if (isMaxLon) {
            // right (East) border
            sit->second.set_left(left);
            sit->second.set_top(top);
            sit->second.set_bottom(bottom);
            sit->second.set_top_left(top_left);
            sit->second.set_bottom_left(bottom_left);
        } else if (isMinLat) {
            // Bottom (South) border
            sit->second.set_right(right);
            sit->second.set_left(left);
            sit->second.set_top(top);
            sit->second.set_top_left(top_left);
            sit->second.set_top_right(top_right);
        } else {
            std::cout << "Error, no match to any case! (square " << this_id << ")" << std::endl;
        }
    }
    
    
}

void tsunamisquares::World::computeNeighborCoords(void) {
    std::map<UIndex, Square>::iterator     sit;

    // precompute the local coordinates of each neighbor, used in plane fitting
    for(sit=_squares.begin(); sit!=_squares.end(); ++sit){
    	std::map<UIndex, Vec<2> >           thisNeighborsAndCoords;
        SquareIDSet				            neighborIDs;
        SquareIDSet::const_iterator         nit;
        Vec<2>                              thispos;

        thispos = sit->second.xy();

        neighborIDs = sit->second.get_neighbors_and_self();
    	for(nit=neighborIDs.begin(); nit!=neighborIDs.end(); nit++){

    		Vec<2> neighpos = square(*nit).xy();

    		double xdiff = (neighpos[0] - thispos[0]);
			double ydiff = (neighpos[1] - thispos[1]);
			int xsign = ((xdiff>0)-(xdiff<0));
			int ysign = ((ydiff>0)-(ydiff<0));
    		// best guess at a catch for getting the sign right across the int date line
    		if( (thispos[0]<-90 && neighpos[0]>90) || (thispos[0]>90 && neighpos[0]<-90) ){
    			xsign *= -1;
    		}

    		double xcoord = xsign*bg::distance(point_spheq(thispos[0], thispos[1]),
    									 point_spheq(neighpos[0], thispos[1])) * EARTH_MEAN_RADIUS;
			double ycoord = ysign*bg::distance(point_spheq(thispos[0], thispos[1]),
										 point_spheq(thispos[0], neighpos[1])) * EARTH_MEAN_RADIUS;
    	    thisNeighborsAndCoords.insert(std::make_pair(*nit, Vec<2>(xcoord, ycoord)));
    	}

    	sit->second.set_local_neighbor_coords(thisNeighborsAndCoords);
    }

}

void tsunamisquares::World::computeEdgeStatus(void) {
    std::map<UIndex, Square>::iterator sit;
    SquareIDSet::iterator nit;
    SquareIDSet neighbor_ids;

    // Set edge status: 0 = active simulation, 1 = second to edge (kill acceleration), 2 = edge (no accel or movement)

    // Find edge squares and directions that water in each square is not allowed to flow
    for (sit=_squares.begin(); sit!=_squares.end(); ++sit) {
        std::vector<bool>       new_invalid_directions(4, false);

    	if(sit->second.left() == INVALID_INDEX){// || square(sit->second.left()).xyz()[2]>=0.0){
        	new_invalid_directions[0] = true;
        	sit->second.set_edge_status(2);
        }
        if(sit->second.right() == INVALID_INDEX){// || square(sit->second.right()).xyz()[2]>=0.0){
        	new_invalid_directions[1] = true;
        	sit->second.set_edge_status(2);
        }
        if(sit->second.bottom() == INVALID_INDEX){// || square(sit->second.bottom()).xyz()[2]>=0.0){
        	new_invalid_directions[2] = true;
        	sit->second.set_edge_status(2);
        }
        if(sit->second.top() == INVALID_INDEX){// || square(sit->second.top()).xyz()[2]>=0.0){
        	new_invalid_directions[3] = true;
        	sit->second.set_edge_status(2);
        }

        sit->second.set_invalid_directions(new_invalid_directions);
    }

    // Find second-to-edge squares
    for (sit=_squares.begin(); sit!=_squares.end(); ++sit) {
    	if(sit->second.edge_status()!=2){
    		neighbor_ids = sit->second.get_valid_neighbors();
    		for(nit=neighbor_ids.begin(); nit!=neighbor_ids.end(); ++nit){
    			if(square(*nit).edge_status() == 2){
    				sit->second.set_edge_status(1);
    			}
    		}
    	}
    }

}


// ----------------------------------------------------------------------
// -------------------- Single Square Functions -------------------------
// ----------------------------------------------------------------------
tsunamisquares::Vec<2> tsunamisquares::World::squareCenter(const UIndex &square_id) const {
    std::map<UIndex, Square>::const_iterator  sit = _squares.find(square_id);
    // (x,y) of square center
    return sit->second.xy();
}

double tsunamisquares::World::squareDepth(const UIndex &square_id) const {
    std::map<UIndex, Square>::const_iterator  sit = _squares.find(square_id);
    // altitude of the sea floor below this square (negative below sea level)
    return sit->second.xyz()[2];
}

double tsunamisquares::World::squareLevel(const UIndex &square_id) const {
    std::map<UIndex, Square>::const_iterator  sit = _squares.find(square_id);
    // altitude of the water surface for this square
    // = altitude of sea floor + height of water
    return (sit->second.xyz()[2])+(sit->second.height());
}
tsunamisquares::Vec<2> tsunamisquares::World::squareLatLon(const UIndex &square_id) const {
	std::map<UIndex, Square>::const_iterator  sit = _squares.find(square_id);
	return Vec<2>(sit->second.xy()[1], sit->second.xy()[0]);
}


// ----------------------------------------------------------------------
// -------------------- Functions to set initial conditions  ------------
// ----------------------------------------------------------------------
void tsunamisquares::World::setSquareVelocity(const UIndex &square_id, const Vec<2> &new_velo) {
    std::map<UIndex, Square>::iterator square_it = _squares.find(square_id);
    square_it->second.set_velocity(new_velo);
}

void tsunamisquares::World::setSquareAccel(const UIndex &square_id, const Vec<2> &new_accel) {
    std::map<UIndex, Square>::iterator square_it = _squares.find(square_id);
    square_it->second.set_accel(new_accel);
}

void tsunamisquares::World::setSquareHeight(const UIndex &square_id, const double &new_height) {
    std::map<UIndex, Square>::iterator square_it = _squares.find(square_id);
    square_it->second.set_height(new_height);
}

// ----------------------------------------------------------------------
// -------------------- Model Building/Editing --------------------------
// ----------------------------------------------------------------------
tsunamisquares::SquareIDSet tsunamisquares::World::getSquareIDs(void) const {
    SquareIDSet square_id_set;
    std::map<UIndex, Square>::const_iterator  sit;

    for (sit=_squares.begin(); sit!=_squares.end(); ++sit) {
        square_id_set.insert(sit->second.id());
    }

    return square_id_set;
}

tsunamisquares::Square &tsunamisquares::World::square(const UIndex &ind) {
    std::map<UIndex, Square>::iterator it = _squares.find(ind);

    if (it == _squares.end()) throw std::domain_error("tsunamisquares::World::square");
    else return it->second;
}


const tsunamisquares::Square &tsunamisquares::World::const_square(const UIndex &ind) const {
    std::map<UIndex, Square>::const_iterator it = _squares.find(ind);

    if (it == _squares.end()) throw std::domain_error("tsunamisquares::World::square");
    else return it->second;
}

tsunamisquares::Square &tsunamisquares::World::new_square(void) {
    UIndex  max_ind = next_square_index();
    _squares.insert(std::make_pair(max_ind, Square()));
    _squares.find(max_ind)->second.set_id(max_ind);
    return _squares.find(max_ind)->second;
}

void tsunamisquares::World::clear(void) {
    _squares.clear();
}

void tsunamisquares::World::insert(tsunamisquares::Square &new_square) {
    _squares.insert(std::make_pair(new_square.id(), new_square));
}

size_t tsunamisquares::World::num_squares(void) const {
    return _squares.size();
}

void tsunamisquares::World::printSquare(const UIndex square_id) {
    Square this_square = square(square_id);

    std::cout << "\n~~~ Square " << this_square.id() << "~~~" << std::endl;
    std::cout << "center:\t " << squareCenter(square_id) << std::endl;
    std::cout << "area:\t " << this_square.area() << std::endl;
	std::cout << "height:\t " << this_square.height() << std::endl;
	std::cout << "level:\t " << squareLevel(square_id) << std::endl;
	std::cout << "volume:\t " << this_square.volume() << std::endl;
	std::cout << "mass:\t " << this_square.mass() << std::endl;
	std::cout << "velocity:\t " << this_square.velocity() << std::endl;
	std::cout << "accel:\t " << this_square.accel() << std::endl;
	std::cout << "momentum:\t " << this_square.momentum() << std::endl;
}

void tsunamisquares::World::info(void) const{
    std::cout << "World: " << this->num_squares() << " squares. " << std::endl;
}

void tsunamisquares::World::get_bounds(LatLonDepth &minimum, LatLonDepth &maximum) const {
    std::map<UIndex, Square>::const_iterator    sit;
    double      min_lat, min_lon, min_alt;
    double      max_lat, max_lon, max_alt;

    min_lat = min_lon = min_alt = DBL_MAX;
    max_lat = max_lon = max_alt = -DBL_MAX;

    for (sit=_squares.begin(); sit!=_squares.end(); ++sit) {
        min_lat = fmin(min_lat, sit->second.lld().lat());
        max_lat = fmax(max_lat, sit->second.lld().lat());
        min_lon = fmin(min_lon, sit->second.lld().lon());
        max_lon = fmax(max_lon, sit->second.lld().lon());
        min_alt = fmin(min_alt, sit->second.lld().altitude());
        max_alt = fmax(max_alt, sit->second.lld().altitude());
    }

    if (min_lat == DBL_MAX || min_lon == DBL_MAX || min_alt == DBL_MAX) {
        minimum = LatLonDepth();
    } else {
        minimum = LatLonDepth(min_lat, min_lon, min_alt);
    }

    if (max_lat == -DBL_MAX || max_lon == -DBL_MAX || max_alt == -DBL_MAX) {
        maximum = LatLonDepth();
    } else {
        maximum = LatLonDepth(max_lat, max_lon, max_alt);
    }
}


// ----------------------------------------------------------------------
// ----------------------------- Model File I/O -------------------------
// ----------------------------------------------------------------------
std::string tsunamisquares::ModelIO::next_line(std::istream &in_stream) {
    std::string line = "";
    size_t      pos;

    do {
        std::getline(in_stream, line);
        _comment = "";
        // Cut off any initial whitespace
        pos = line.find_first_not_of(" \t");

        if (pos != std::string::npos) line = line.substr(pos, std::string::npos);

        // Comment consists of hash mark until the end of the line
        pos = line.find("#");

        if (pos != std::string::npos) _comment = line.substr(pos, std::string::npos);

        // Extract the non-comment part of the line
        line = line.substr(0, line.find("#"));

        // If the line is empty, we keep going
        if (line.length() > 0) break;
    } while (in_stream && !in_stream.eof());

    return line;
}

void tsunamisquares::ModelIO::next_line(std::ostream &out_stream) const {
    if (!_comment.empty()) out_stream << " # " << _comment;

    out_stream << "\n";
}

void tsunamisquares::World::write_square_ascii(std::ostream &out_stream, const double &time, const UIndex &square_id) const {
    unsigned int        i;
    std::map<UIndex, Square>::const_iterator square_it = _squares.find(square_id);
    double waterLevel, waterHeight;

    out_stream << time << "\t";

    //
    out_stream << square_it->second.xy()[1] << "\t\t" << square_it->second.xy()[0] << "\t\t";

    // Don't write water level for the hi and dry squares until they take on water
    waterLevel  = square_it->second.height() + square_it->second.xyz()[2];
    waterHeight = square_it->second.height();
    if (waterHeight == 0.0 && waterLevel >= 0.0) {
        out_stream << waterHeight << "\t\t";
    } else {
        out_stream << waterLevel << "\t\t";
    }
    
    // Write the altitude of the bottom too
    out_stream << square_it->second.xyz()[2] << "\t\t";

    next_line(out_stream);
}


void tsunamisquares::World::initilize_netCDF_file(const std::string &file_name){
	std::map<UIndex, Square>::const_iterator sit;
	// We are writing 3D data, a NLAT x NLON lat-lon grid, with NSTEPS timesteps of data.

	// Define nlat and nlon from world data
	int NLAT = num_lats();
	int NLON = num_lons();

	// For the units attributes.
	std::string  UNITS = "units";
	std::string  TIME_UNITS = "seconds";
	std::string  LENGTH_UNITS = "meters";
	std::string  LAT_UNITS = "degrees_north";
	std::string  LON_UNITS = "degrees_east";
	std::string  VELOCITY_UNITS = "meters/second";

	// We will write latitude and longitude fields.
	float lats[NLAT],lons[NLON];
	for(sit=_squares.begin(); sit!=_squares.end(); sit++){
		int lat_index = floor(sit->first / double(NLON));
		int lon_index = sit->first % NLON;

		lats[lat_index] = sit->second.xy()[1];
		lons[lon_index] = sit->second.xy()[0];
	}


	// Create the file.
	NcFile dataFile(file_name, NcFile::replace);

	// Define the dimensions. NetCDF will hand back an ncDim object for
	// each.
	NcDim timeDim = dataFile.addDim("time");  //adds an unlimited dimension
	NcDim latDim = dataFile.addDim("latitude", NLAT);
	NcDim lonDim = dataFile.addDim("longitude", NLON);

	// Define the coordinate variables.
	NcVar timeVar = dataFile.addVar("time", ncFloat, timeDim);
	NcVar latVar = dataFile.addVar("latitude", ncFloat, latDim);
	NcVar lonVar = dataFile.addVar("longitude", ncFloat, lonDim);

	// Define units attributes for coordinate vars. This attaches a
	// text attribute to each of the coordinate variables, containing
	// the units.
	timeVar.putAtt(UNITS, TIME_UNITS);
	latVar.putAtt(UNITS, LAT_UNITS);
	lonVar.putAtt(UNITS, LON_UNITS);


	//Dimension vector for correctly sizing data variables
	std::vector<NcDim> dimVector;
	dimVector.push_back(timeDim);
	dimVector.push_back(latDim);
	dimVector.push_back(lonDim);

	// Define the netCDF variables for the data
	NcVar levelVar = dataFile.addVar("level", ncFloat, dimVector);
	NcVar heightVar = dataFile.addVar("height", ncFloat, dimVector);
	NcVar altVar = dataFile.addVar("altitude", ncFloat, dimVector);
	NcVar velHorVar = dataFile.addVar("horizontal velocity", ncFloat, dimVector);
	NcVar velVertVar = dataFile.addVar("vertical velocity", ncFloat, dimVector);

	// Define units attributes for coordinate vars. This attaches a
	// text attribute to each of the coordinate variables, containing
	// the units.
	levelVar.putAtt(UNITS, LENGTH_UNITS);
	heightVar.putAtt(UNITS, LENGTH_UNITS);
	altVar.putAtt(UNITS, LENGTH_UNITS);
	velHorVar.putAtt(UNITS, VELOCITY_UNITS);
	velVertVar.putAtt(UNITS, VELOCITY_UNITS);

	// Write the coordinate variable data to the file.
	latVar.putVar(lats);
	lonVar.putVar(lons);
}

void tsunamisquares::World::append_netCDF_file(const std::string &file_name, const int &current_step, const float &this_time){
	std::map<UIndex, Square>::const_iterator sit;

	NcFile dataFile(file_name, NcFile::write);

	long NLAT = dataFile.getDim("latitude").getSize();
	long NLON = dataFile.getDim("longitude").getSize();

	// Get the variables we'll be writing to
	NcVar timeVar = dataFile.getVar("time");
	NcVar levelVar = dataFile.getVar("level");
	NcVar heightVar = dataFile.getVar("height");
	NcVar altVar = dataFile.getVar("altitude");
	NcVar velHorVar = dataFile.getVar("horizontal velocity");
	NcVar velVertVar = dataFile.getVar("vertical velocity");

	float time_out[1];
	float level_out[NLAT][NLON];
	float height_out[NLAT][NLON];
	float alt_out[NLAT][NLON];
	float velHor_out[NLAT][NLON];
	float velVert_out[NLAT][NLON];

	// Populate the data arrays from world data
	time_out[0] = this_time;
	for(sit=_squares.begin(); sit!=_squares.end(); sit++){
		int lat_index = std::floor(sit->first / double(NLON));
		int lon_index = sit->first % NLON;

		level_out[lat_index][lon_index] = squareLevel(sit->first);
		height_out[lat_index][lon_index] = sit->second.height();
		alt_out[lat_index][lon_index] = sit->second.xyz()[2];
		velHor_out[lat_index][lon_index] = sit->second.velocity()[0];
		velVert_out[lat_index][lon_index] = sit->second.velocity()[1];
	}


	std::vector<size_t> startp, countp, starttime, counttime;

	// Starting index of data to be written.  Arrays at each time step start at [timeStep, 0, 0]
	starttime.push_back(current_step);

	startp.push_back(current_step);
	startp.push_back(0);
	startp.push_back(0);

	// Size of array to be written.  One time step, nlats and nlons in those dims.
	counttime.push_back(1);

	countp.push_back(1);
	countp.push_back(NLAT);
	countp.push_back(NLON);

	timeVar.putVar(starttime,counttime,time_out);
	levelVar.putVar(startp,countp,level_out);
	heightVar.putVar(startp,countp,height_out);
	altVar.putVar(startp,countp,alt_out);
	velHorVar.putVar(startp,countp,velHor_out);
	velVertVar.putVar(startp,countp,velVert_out);
}

void tsunamisquares::World::write_sim_state_netCDF(const std::string &file_name, const float &this_time){
	std::map<UIndex, Square>::const_iterator sit;
	// We are writing 2D data, a NLAT x NLON lat-lon grid

	// Define nlat and nlon from world data
	int NLAT = num_lats();
	int NLON = num_lons();

	// For the units attributes.
	std::string  UNITS = "units";
	std::string  TIME_UNITS = "seconds";
	std::string  LENGTH_UNITS = "meters";
	std::string  VELOCITY_UNITS = "meters/second";
	std::string  LAT_UNITS = "degrees_north";
	std::string  LON_UNITS = "degrees_east";

	// We will write latitude and longitude fields.
	// Populate the data arrays from world data
	float lats[NLAT],lons[NLON];
	float time_out[1];
	float level_out[NLAT][NLON];
	float height_out[NLAT][NLON];
	float alt_out[NLAT][NLON];
	float velocity_horiz_out[NLAT][NLON];
	float velocity_vert_out[NLAT][NLON];

	time_out[0] = this_time;
	for(sit=_squares.begin(); sit!=_squares.end(); sit++){
		int lat_index = floor(sit->first / double(NLON));
		int lon_index = sit->first % NLON;

		lats[lat_index] = sit->second.xy()[1];
		lons[lon_index] = sit->second.xy()[0];

		level_out[lat_index][lon_index] = squareLevel(sit->first);
		height_out[lat_index][lon_index] = sit->second.height();
		alt_out[lat_index][lon_index] = sit->second.xyz()[2];
		velocity_horiz_out[lat_index][lon_index] = sit->second.velocity()[0];
		velocity_vert_out[lat_index][lon_index] = sit->second.velocity()[1];
	}

	// Create the file.
	NcFile dataFile(file_name, NcFile::replace);

	// Define the dimensions. NetCDF will hand back an ncDim object for
	// each.
	NcDim timeDim = dataFile.addDim("time"); //unlimited dimension
	NcDim latDim = dataFile.addDim("latitude", NLAT);
	NcDim lonDim = dataFile.addDim("longitude", NLON);

	// Define the coordinate variables.
	NcVar timeVar = dataFile.addVar("time", ncFloat, timeDim);
	NcVar latVar = dataFile.addVar("latitude", ncFloat, latDim);
	NcVar lonVar = dataFile.addVar("longitude", ncFloat, lonDim);

	// Define units attributes for coordinate vars. This attaches a
	// text attribute to each of the coordinate variables, containing
	// the units.
	timeVar.putAtt(UNITS, TIME_UNITS);
	latVar.putAtt(UNITS, LAT_UNITS);
	lonVar.putAtt(UNITS, LON_UNITS);

	//Dimension vector for correctly sizing data variables
	std::vector<NcDim> dimVector;
	dimVector.push_back(timeDim);
	dimVector.push_back(latDim);
	dimVector.push_back(lonDim);

	// Define the netCDF variables for the data
	NcVar levelVar = dataFile.addVar("level", ncFloat, dimVector);
	NcVar heightVar = dataFile.addVar("height", ncFloat, dimVector);
	NcVar altVar = dataFile.addVar("altitude", ncFloat, dimVector);
	NcVar velHorizVar = dataFile.addVar("horizontal velocity", ncFloat, dimVector);
	NcVar velVertVar = dataFile.addVar("vertical velocity", ncFloat, dimVector);

	// Define units attributes for coordinate vars. This attaches a
	// text attribute to each of the coordinate variables, containing
	// the units.
	levelVar.putAtt(UNITS, LENGTH_UNITS);
	heightVar.putAtt(UNITS, LENGTH_UNITS);
	altVar.putAtt(UNITS, LENGTH_UNITS);
	velHorizVar.putAtt(UNITS, VELOCITY_UNITS);
	velVertVar.putAtt(UNITS, VELOCITY_UNITS);

	// Write the coordinate variable data to the file.
	latVar.putVar(lats);
	lonVar.putVar(lons);

	std::vector<size_t> starttime, counttime, startp, countp;
	// Starting index of data to be written.  Arrays at each time step start at [timeStep, 0, 0]
	starttime.push_back(0);

	startp.push_back(0);
	startp.push_back(0);
	startp.push_back(0);

	// Size of array to be written.  One time step, nlats and nlons in those dims.
	counttime.push_back(1);

	countp.push_back(1);
	countp.push_back(NLAT);
	countp.push_back(NLON);

	timeVar.putVar(starttime, counttime, time_out);
	levelVar.putVar(startp,countp,level_out);
	heightVar.putVar(startp,countp,height_out);
	altVar.putVar(startp,countp,alt_out);
	velHorizVar.putVar(startp,countp,velocity_horiz_out);
	velVertVar.putVar(startp,countp,velocity_vert_out);
}


void tsunamisquares::World::read_sim_state_netCDF(const std::string &file_name){
	// Read in NetCDF
	NcFile dataFile(file_name, NcFile::read);

	long NTIME = dataFile.getDim("time").getSize();
	long NLAT = dataFile.getDim("latitude").getSize();
	long NLON = dataFile.getDim("longitude").getSize();

	// These will hold our data.
	float lats_in[NLAT], lons_in[NLON];
	float level_in[NLAT][NLON];
	float height_in[NLAT][NLON];
	float alt_in[NLAT][NLON];
	float velocity_horiz_in[NLAT][NLON];
	float velocity_vert_in[NLAT][NLON];

	// Get the variables
	NcVar timeVar, latVar, lonVar;
	timeVar = dataFile.getVar("time");
	latVar = dataFile.getVar("latitude");
	lonVar = dataFile.getVar("longitude");
	latVar.getVar(lats_in);
	lonVar.getVar(lons_in);

	NcVar levelVar, heightVar, altVar, velHorizVar, velVertVar;
	levelVar = dataFile.getVar("level");
	heightVar = dataFile.getVar("height");
	altVar = dataFile.getVar("altitude");
	velHorizVar = dataFile.getVar("horizontal velocity");
	velVertVar = dataFile.getVar("vertical velocity");

	// corner location and edge size vectors for slicing into last time step of netCDF vars
	std::vector<size_t> startp, countp;

	startp.push_back(NTIME-1);
	startp.push_back(0);
	startp.push_back(0);

	countp.push_back(1);
	countp.push_back(NLAT);
	countp.push_back(NLON);

	// Slice into the netCDF vars and store them in the arrays
	levelVar.getVar(startp, countp, level_in);
	heightVar.getVar(startp, countp, height_in);
	altVar.getVar(startp, countp, alt_in);
	velHorizVar.getVar(startp, countp, velocity_horiz_in);
	velVertVar.getVar(startp, countp, velocity_vert_in);

	/* Make 2d interpolation from each input arrays (from netCDF file)
	 *
	 * Do rtree overlap between input bounds and this world square rtree to get set of squares that are affected by input file
	 *
	 * Loop through concerned squares, set all their values from the 2d interpolations from input
	 *    Make sure there's an if(!flatten_bool) before setting altitude
	 */

	// put input data into alglib real_1d_arrays
	real_1d_array lon_algarr, lat_algarr;
	lon_algarr.setlength(NLON);
	lat_algarr.setlength(NLAT);
	real_1d_array level_algarr, height_algarr, alt_algarr, velHor_algarr, velVert_algarr;
	level_algarr.setlength(NLON*NLAT);
	height_algarr.setlength(NLON*NLAT);
	alt_algarr.setlength(NLON*NLAT);
	velHor_algarr.setlength(NLON*NLAT);
	velVert_algarr.setlength(NLON*NLAT);
	for(int j=0; j<NLAT; j++){
		lat_algarr[j] = lats_in[j];
		for(int i=0; i<NLON; i++){
			lon_algarr[i] = lons_in[i];
			level_algarr[j*NLON+i]  = level_in[j][i];
			height_algarr[j*NLON+i]  = height_in[j][i];
			alt_algarr[j*NLON+i]     = alt_in[j][i];
			velHor_algarr[j*NLON+i]  = velocity_horiz_in[j][i];
			velVert_algarr[j*NLON+i] = velocity_vert_in[j][i];
		}
	}

	// Build splines
	spline2dinterpolant level_spline, height_spline, alt_spline, velHor_spline, velVert_spline;
	spline2dbuildbicubicv(lon_algarr, NLON, lat_algarr, NLAT, level_algarr,  1, level_spline);
	spline2dbuildbicubicv(lon_algarr, NLON, lat_algarr, NLAT, height_algarr,  1, height_spline);
	spline2dbuildbicubicv(lon_algarr, NLON, lat_algarr, NLAT, alt_algarr,     1, alt_spline);
	spline2dbuildbicubicv(lon_algarr, NLON, lat_algarr, NLAT, velHor_algarr,  1, velHor_spline);
	spline2dbuildbicubicv(lon_algarr, NLON, lat_algarr, NLAT, velVert_algarr, 1, velVert_spline);

	// Do rtree grab of all squares that need their initial conditions set
	point_spheq top_left_in, top_right_in, bottom_right_in, bottom_left_in;
	float minlon = lons_in[0];
	float maxlon = lons_in[NLON-1];
	float minlat = lats_in[0];
	float maxlat = lats_in[NLAT-1];
	top_left_in = point_spheq(minlon, maxlat);
	top_right_in = point_spheq(maxlon, maxlat);
	bottom_right_in = point_spheq(maxlon, minlat);
	bottom_left_in = point_spheq(minlon, minlat);

	point_spheq ring_verts[5] = {bottom_left_in, top_left_in, top_right_in, bottom_right_in, bottom_left_in};
	ring_spheq new_ring;
	bg::assign_points(new_ring, ring_verts);
	SquareIDSet intersected_squares;
	intersected_squares = _square_rtree.getRingIntersects(new_ring);


	// For squares in the subset grabbed by rtree search, set values to those from input
	SquareIDSet::const_iterator sidit;
	for(sidit=intersected_squares.begin(); sidit !=intersected_squares.end(); sidit++){
		std::map<UIndex, Square>::iterator sit = _squares.find(*sidit);
		float square_lon = sit->second.xy()[0];
		float square_lat = sit->second.xy()[1];

		// Set square altitude
		LatLonDepth new_lld = sit->second.lld();
		new_lld.set_altitude( spline2dcalc(alt_spline, square_lon, square_lat) );
		sit->second.set_lld(new_lld);

		// Most important thing for hazard estimate is level of water, not how deep the column is.  So get appropriate height from level.
		float heighttoset = spline2dcalc(level_spline, square_lon, square_lat) - spline2dcalc(alt_spline, square_lon, square_lat);

		// If the amount of water is tiny, best to not give the square any at all.  This avoids artifically wetting dry land with miniscule amounts.
		//  Only seafloor below a certain depth will recieve any water, so don't try to pick up a simulation that has already inundated land.
		if(heighttoset > 1e-2 && sit->second.xyz()[2] < -1e2){
			sit->second.set_height( heighttoset );
			sit->second.set_velocity( Vec<2>( spline2dcalc(velHor_spline, square_lon, square_lat), spline2dcalc(velVert_spline, square_lon, square_lat) ) );
		}


	}

}


int tsunamisquares::World::read_bathymetry_chooser(const std::string &file_name){

	if(file_name.substr(file_name.find_last_of(".")+1) == "txt"){
		read_bathymetry_txt(file_name);
	} else if(file_name.substr(file_name.find_last_of(".")+1) == "nc"){
		read_bathymetry_netCDF(file_name);
	} else {
		throw std::invalid_argument( "Bathymetry file type not recognized" );
	}

	return 0;
}


int tsunamisquares::World::read_bathymetry_txt(const std::string &file_name) {
    std::ifstream   in_file;
    UIndex          i, j, num_squares, num_vertices, num_lats, num_lons;
    LatLonDepth     min_latlon, max_latlon;
    float			Lx_tot = 0.0;
	float			Ly_tot = 0.0;
	float			dlon, dlat;

    // Clear the world first to avoid incorrectly mixing indices
    clear();

    in_file.open(file_name.c_str());

    if (!in_file.is_open()) return -1;

    // Read the first line of metadata
    std::stringstream desc_line(next_line(in_file));
    desc_line >> num_lats;
    desc_line >> num_lons;
    desc_line >> dlat;
	desc_line >> dlon;
    _num_latitudes = num_lats;
    _num_longitudes = num_lons;
    _dlat = dlat;
	_dlon = dlon;

    // Set the number of squares
    num_squares = num_lats*num_lons;

    // Read squares, populate world rtree and square map.
    for (i=0; i<num_squares; ++i) {
        Square     new_square;
        new_square.set_id(i);
        new_square.read_bathymetry(in_file);
        new_square.set_box(dlon, dlat);
        _square_rtree.addPoint(new_square.xy(), new_square.id());
        _squares.insert(std::make_pair(new_square.id(), new_square));
    }

    in_file.close();
        
    // Get world lld bounds
    get_bounds(min_latlon, max_latlon);
    min_latlon.set_altitude(0); //Why is this here?

    // Keep track of Lat/Lon bounds in the World
    _min_lat = min_latlon.lat();
    _min_lon = min_latlon.lon();
    _max_lat = max_latlon.lat();
    _max_lon = max_latlon.lon();

    // Find max depth and min cell spacing
    calcMinSpacing();
    calcMaxDepth();

    return 0;
}

int tsunamisquares::World::read_bathymetry_netCDF(const std::string &file_name) {
	// Clear the world first to avoid incorrectly mixing indices
	clear();

	// Read in NetCDF
	NcFile dataFile(file_name, NcFile::read);

	long num_lats = dataFile.getDim("latitude").getSize();
	long num_lons = dataFile.getDim("longitude").getSize();

	// These will hold our data.
	float lats_in[num_lats], lons_in[num_lons];
	float alt_in[num_lats][num_lons];

	// Get the variables
	NcVar latVar, lonVar;
	latVar = dataFile.getVar("latitude");
	lonVar = dataFile.getVar("longitude");
	latVar.getVar(lats_in);
	lonVar.getVar(lons_in);

	NcVar altVar;
	altVar = dataFile.getVar("altitude");
	altVar.getVar(alt_in);

	// Set metadata
	float dlat = fabs(lats_in[1]-lats_in[0]);
	float dlon = fabs(lons_in[1]-lons_in[0]);
	_num_latitudes = num_lats;
	_num_longitudes = num_lons;
	_dlat = dlat;
	_dlon = dlon;

	// Read squares, populate world rtree and square map.
	for(int j=0; j<num_lats; j++){
		for(int i=0; i<num_lons; i++){
			Square     new_square;
			new_square.set_id((j*num_lons) + i);

			// Do all the stuff that Square::read_bathymetry() does
			LatLonDepth new_lld;
			new_lld.set_lon(lons_in[i]);
			new_lld.set_lat(lats_in[j]);
			new_lld.set_altitude(alt_in[j][i]);
			new_square.set_lld(new_lld);

			new_square.set_box(dlon, dlat);
			_square_rtree.addPoint(new_square.xy(), new_square.id());
			_squares.insert(std::make_pair(new_square.id(), new_square));
		}
	}

	// Get world lld bounds
    LatLonDepth     min_latlon, max_latlon;
	get_bounds(min_latlon, max_latlon);
	min_latlon.set_altitude(0); //Why is this here?

	// Keep track of Lat/Lon bounds in the World
	_min_lat = min_latlon.lat();
	_min_lon = min_latlon.lon();
	_max_lat = max_latlon.lat();
	_max_lon = max_latlon.lon();

	// Find max depth and min cell spacing
	calcMinSpacing();
	calcMaxDepth();

	return 0;
}


void tsunamisquares::World::populate_wet_rtree(void){
	std::map<UIndex, Square>::const_iterator sit;
	for(sit=_squares.begin(); sit != _squares.end(); sit++){
		if(sit->second.xyz()[2] < 0) _wet_rtree.addPoint(sit->second.xy(), sit->first);
									//_wet_rtree.addBox(new_square.box(), new_square.id());
	}
	return;
}


int tsunamisquares::World::write_file_kml(const std::string &file_name) {
    std::ofstream                             out_file;
    std::map<UIndex, Square>::const_iterator  sit;
    LatLonDepth                               min_bound, max_bound, center;
    Vec<3>                                    min_xyz, max_xyz;
    double                                    dx, dy, range, Lx, Ly;
    unsigned int                              i;
    double                                    depth = 100; //So the squares are off the surface a bit

    out_file.open(file_name.c_str());

    get_bounds(min_bound, max_bound);
    center = LatLonDepth(max_bound.lat() - (max_bound.lat()-min_bound.lat())/2,
                         max_bound.lon() - (max_bound.lon()-min_bound.lon())/2);
    Conversion c(center);
    min_xyz = c.convert2xyz(min_bound);
    max_xyz = c.convert2xyz(max_bound);
    dx = max_xyz[0]-min_xyz[0];
    dy = max_xyz[1]-min_xyz[1];
    range = fmax(dx, dy) * (1.0/tan(c.deg2rad(30)));

    out_file << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out_file << "<kml xmlns=\"http://www.opengis.net/kml/2.2\">\n";
    out_file << "<Document>\n";
    out_file << "<LookAt>\n";
    out_file << "\t<latitude>" << center.lat() << "</latitude>\n";
    out_file << "\t<longitude>" << center.lon() << "</longitude>\n";
    out_file << "\t<altitude>0</altitude>\n";
    out_file << "\t<range>" << range << "</range>\n";
    out_file << "\t<tilt>0</tilt>\n";
    out_file << "\t<heading>0</heading>\n";
    out_file << "\t<altitudeMode>absolute</altitudeMode>\n";
    out_file << "</LookAt>\n";
    out_file << "<Style id=\"sectionLabel\">\n";
    out_file << "\t<IconStyle>\n";
    out_file << "\t\t<Icon>\n";
    out_file << "\t\t\t<href>http://maps.google.com/mapfiles/kml/paddle/wht-blank.png</href>\n";
    out_file << "\t\t</Icon>\n";
    out_file << "\t</IconStyle>\n";
    out_file << "</Style>\n";

    out_file << "<Folder id=\"squares\">\n";

    for (sit=_squares.begin(); sit!=_squares.end(); ++sit) {
        // Compute the lat/lon/depth of the 4 corners of the square
        LatLonDepth         lld[4];
        Vec<2>              v2, centerXY;
        Vec<3>              v3;
        LatLonDepth         centerLLD, base;

        base        = getBase();
        centerLLD   = sit->second.lld();
        centerXY    = squareCenter(sit->first);    
        Conversion  c(base);
        Lx          = _dlon;
        Ly          = _dlat;
        // Locate the corners in XYZ, then convert to LLD
        v3      = Vec<3>(centerXY[0]-Lx/2.0, centerXY[1]+Ly/2, 0.0); // top left
        lld[0]  = c.convert2LatLon(v3);
        v3      = Vec<3>(centerXY[0]-Lx/2.0, centerXY[1]-Ly/2, 0.0); // bottom left
        lld[1]  = c.convert2LatLon(v3);
        v3      = Vec<3>(centerXY[0]+Lx/2.0, centerXY[1]-Ly/2, 0.0); // bottom right
        lld[2]  = c.convert2LatLon(v3);
        v3      = Vec<3>(centerXY[0]+Lx/2.0, centerXY[1]+Ly/2, 0.0); // top left
        lld[3]  = c.convert2LatLon(v3);
        
        // Output the KML format polygon for this section
        out_file << "\t\t<Placemark>\n";
        out_file << "\t\t<description>\n";
        out_file << "Square: " << sit->first << "\n";
        out_file << "LLD: " << squareLatLon(sit->first)[0] << "," << squareLatLon(sit->first)[1] << "," << squareDepth(sit->first) << " [m]\n";
        out_file << "XYZ: " << squareCenter(sit->first)[0] << "," << squareCenter(sit->first)[1] << ","   << squareDepth(sit->first) << " [m]\n";
        out_file << "Area: " << sit->second.area()*pow(10,-6) << "[km^2]\n";
        out_file << "Density: " << sit->second.density() << "[kg/m^3]\n";
        out_file << "\t\t</description>\n";
        out_file << "\t\t\t<styleUrl>#baseStyle</styleUrl>\n";
        out_file << "\t\t\t<Polygon>\n";
        out_file << "\t\t\t\t<extrude>0</extrude>\n";
        out_file << "\t\t\t\t<altitudeMode>relativeToGround</altitudeMode>\n";
        out_file << "\t\t\t\t<outerBoundaryIs>\n";
        out_file << "\t\t\t\t\t<LinearRing>\n";
        out_file << "\t\t\t\t\t\t<coordinates>\n";

        for (unsigned int i=0; i<4; ++i) out_file << "\t\t\t\t\t\t\t" << lld[i].lon() << "," << lld[i].lat() << "," << depth << "\n";

        out_file << "\t\t\t\t\t\t</coordinates>\n";
        out_file << "\t\t\t\t\t</LinearRing>\n";
        out_file << "\t\t\t\t</outerBoundaryIs>\n";
        out_file << "\t\t\t</Polygon>\n";
        out_file << "\t\t</Placemark>\n";
    }

    out_file << "</Folder>\n";
    out_file << "</Document>\n";
    out_file << "</kml>\n";

    out_file.close();

    return 0;
}


int tsunamisquares::World::deformFromFile_txt(const std::string &file_name) {
    std::ifstream   in_file;
    UIndex          i, num_points, mappedID;
    double          dz;
    Vec<2>          location;
    std::map<UIndex, Square>::iterator sit;
    std::multimap<double, UIndex>	   nearestMap;
    LatLonDepth     square_lld;

    in_file.open(file_name.c_str());

    if (!in_file.is_open()) return -1;

    // Read the first line describing the number of sections, etc
    std::stringstream desc_line(next_line(in_file));
    desc_line >> num_points;

    // Read the points, find nearest square, deform the bottom
    for (i=0; i<num_points; ++i) {
        Square     new_square;
        new_square.read_bathymetry(in_file);
        
        // Get location (x,y) for the lat/lon point and get the altitude change
        location = new_square.xy();
        dz  = new_square.xyz()[2];
        
        // Find the closest square, grab its vertex
        // Use the RTree here as well for speed
        nearestMap = getNearest_rtree(new_square.xy(), 1, false);
        mappedID   = nearestMap.begin()->second;

        sit = _squares.find( mappedID );
        
        // Get the current LLD data for this closest square
        square_lld = sit->second.lld();
        
        // Update the altitude of the vertex by the amount dz
        square_lld.set_altitude(square_lld.altitude() + dz);
        
        // Set the new position
        sit->second.set_lld(square_lld);
        
    }

    in_file.close();

    return 0;
}


void tsunamisquares::World::deformFromFile_netCDF(const std::string &file_name) {

	// Read in NetCDF
	NcFile dataFile(file_name, NcFile::read);

	long NLAT = dataFile.getDim("latitude").getSize();
	long NLON = dataFile.getDim("longitude").getSize();

	// These will hold our data.
	float lats_in[NLAT], lons_in[NLON];
	float uplift_in[NLAT][NLON];
	float eastU_in[NLAT][NLON];
	float northV_in[NLAT][NLON];

	// Get the variables
	NcVar latVar, lonVar;
	latVar = dataFile.getVar("latitude");
	lonVar = dataFile.getVar("longitude");
	latVar.getVar(lats_in);
	lonVar.getVar(lons_in);

	NcVar upliftVar, eastUVar, northVVar;
	upliftVar = dataFile.getVar("uplift");
	eastUVar = dataFile.getVar("east U");
	northVVar = dataFile.getVar("north V");
	upliftVar.getVar(uplift_in);
	eastUVar.getVar(eastU_in);
	northVVar.getVar(northV_in);


	/* Make 2d interpolation from each input arrays (from netCDF file)
	 *
	 * Do rtree overlap between input bounds and this world square rtree to get set of squares that are affected by input file
	 *
	 * Loop through concerned squares, set all their values from the 2d interpolations from input
	 *    Make sure there's an if(!flatten_bool) before setting altitude
	 */

	// put input data into alglib real_1d_arrays
	real_1d_array lon_algarr, lat_algarr;
	lon_algarr.setlength(NLON);
	lat_algarr.setlength(NLAT);
	real_1d_array uplift_algarr, eastU_algarr, northV_algarr;
	uplift_algarr.setlength(NLON*NLAT);
	eastU_algarr.setlength(NLON*NLAT);
	northV_algarr.setlength(NLON*NLAT);
	for(int j=0; j<NLAT; j++){
		lat_algarr[j] = lats_in[j];
		for(int i=0; i<NLON; i++){
			lon_algarr[i] = lons_in[i];
			uplift_algarr[j*NLON+i]  = uplift_in[j][i];
			eastU_algarr[j*NLON+i]  = eastU_in[j][i];
			northV_algarr[j*NLON+i]     = northV_in[j][i];
		}
	}

	// build splines
	spline2dinterpolant uplift_spline, eastU_spline, northV_spline;
	spline2dbuildbicubicv(lon_algarr, NLON, lat_algarr, NLAT, uplift_algarr,  1, uplift_spline);
	spline2dbuildbicubicv(lon_algarr, NLON, lat_algarr, NLAT, eastU_algarr,  1, eastU_spline);
	spline2dbuildbicubicv(lon_algarr, NLON, lat_algarr, NLAT, northV_algarr,     1, northV_spline);

	//Do rtree grab of all squares that need their initial conditions set
	point_spheq top_left_in, top_right_in, bottom_right_in, bottom_left_in;
	float minlon = lons_in[0];
	float maxlon = lons_in[NLON-1];
	float minlat = lats_in[0];
	float maxlat = lats_in[NLAT-1];
	top_left_in = point_spheq(minlon, maxlat);
	top_right_in = point_spheq(maxlon, maxlat);
	bottom_right_in = point_spheq(maxlon, minlat);
	bottom_left_in = point_spheq(minlon, minlat);

	point_spheq ring_verts[5] = {bottom_left_in, top_left_in, top_right_in, bottom_right_in, bottom_left_in};
	ring_spheq new_ring;
	bg::assign_points(new_ring, ring_verts);
	SquareIDSet intersected_squares;
	intersected_squares = _square_rtree.getRingIntersects(new_ring);


	// for squares in the subset grabbed by rtree search, set values to those from input
	SquareIDSet::const_iterator sidit;
	for(sidit=intersected_squares.begin(); sidit !=intersected_squares.end(); sidit++){
		std::map<UIndex, Square>::iterator sit = _squares.find(*sidit);
		float square_lon = sit->second.xy()[0];
		float square_lat = sit->second.xy()[1];

		LatLonDepth new_lld = sit->second.lld();
		new_lld.set_altitude( new_lld.altitude() + spline2dcalc(uplift_spline, square_lon, square_lat) );
		sit->second.set_lld(new_lld);

	}

	return;
}

void tsunamisquares::Square::read_bathymetry(std::istream &in_stream) {
    std::stringstream   ss(next_line(in_stream));

    ss >> _data._lat;
    ss >> _data._lon;
    ss >> _data._alt;
    _pos = Vec<3>(_data._lon, _data._lat, _data._alt);

}




