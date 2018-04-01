/*
    Copyright (C) 2018, Jianwen Li (lijwen2748@gmail.com), Iowa State University

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

/*
	Author: Jianwen Li
	Update Date: October 6, 2017
	Main Solver in CAR
*/


#include "mainsolver.h"
#include "utility.h"

#include <algorithm>
using namespace std;

namespace car
{
	int MainSolver::max_flag_ = -1;
	vector<int> MainSolver::frame_flags_;
	
	MainSolver::MainSolver (Model* m, Statistics* stats, double ratio, const bool verbose) 
	{
	    verbose_ = verbose;
	    stats_ = stats;
	    reduce_ratio_ = ratio;
		model_ = m;
		if (max_flag_ == -1)
			max_flag_ = m->max_id() + 1;
	    //constraints
		for (int i = 0; i < m->outputs_start (); i ++)
			add_clause (m->element (i));
		//outputs
		for (int i = m->outputs_start (); i < m->latches_start (); i ++)
			add_clause (m->element (i));
		//latches
		for (int i = m->latches_start (); i < m->size (); i ++)
		    add_clause (m->element (i));
	}
	
	void MainSolver::set_assumption (const Assignment& st, const int id)
	{
		assumption_.clear ();
		for (Assignment::const_iterator it = st.begin (); it != st.end (); it++)
		{
			assumption_push (*it);
		}
		
		assumption_push (id);
				
	}
	
	void MainSolver::set_assumption (const Assignment& a, const int frame_level, const bool forward)
	{
		assumption_.clear ();
		//Assignment a = const_cast<State*> (st)-> s();
		if (frame_level > -1)
			assumption_push (flag_of (frame_level));
		for (Assignment::const_iterator it = a.begin (); it != a.end (); it ++)
		{
			int id = *it;
			if (forward)
				assumption_push (model_->prime (id));
			else
				assumption_push (id);
		}
				
	}
	
	Assignment MainSolver::get_state (const bool forward, const bool partial)
	{
		Assignment model = get_model ();
		shrink_model (model, forward, partial);
		return model;
	}
	
	//this version is used for bad check only
	Cube MainSolver::get_conflict (const int bad)
	{
		Cube conflict = get_uc ();
		Cube res;
		for (int i = 0; i < conflict.size (); i ++)
		{
			if (conflict[i] != bad)
				res.push_back (conflict[i]);
		}
		return res;
	}
	
	Cube MainSolver::get_conflict (const bool forward, const bool minimal, bool& constraint)
	{
		Cube conflict = get_uc ();
		
		if (minimal)
		{
			stats_->count_orig_uc_size (int (conflict.size ()));
			try_reduce (conflict);
			stats_->count_reduce_uc_size (int (conflict.size ()));
		}
		
			
		if (forward)
		    model_->shrink_to_previous_vars (conflict, constraint);
		else
		    model_ -> shrink_to_latch_vars (conflict, constraint);
		
		
		std::sort (conflict.begin (), conflict.end (), car::comp);
		
		return conflict;
	}
	
	void MainSolver::add_new_frame (const Frame& frame, const int frame_level, const bool forward)
	{
		for (int i = 0; i < frame.size (); i ++)
		{
			add_clause_from_cube (frame[i], frame_level, forward);
		}
	}
	
	void MainSolver::add_clause_from_cube (const Cube& cu, const int frame_level, const bool forward)
	{
		int flag = flag_of (frame_level);
		vector<int> cl;
		cl.push_back (-flag);
		for (int i = 0; i < cu.size (); i ++)
		{
			if (!forward)
				cl.push_back (-model_->prime (cu[i]));
			else
				cl.push_back (-cu[i]);
		}
		add_clause (cl);
	}
	
	void MainSolver::shrink_model (Assignment& model, const bool forward, const bool partial)
	{
	    Assignment res;
	    
	    for (int i = 0; i < model_->num_inputs (); i ++)
	    {
	        if (i >= model.size ())
	        {//the value is DON'T CARE, so we just set to 0
	            res.push_back (0);
	        }
	        else
	            res.push_back (model[i]);
	    }
	        
		if (forward)
		{
		    for (int i = model_->num_inputs (); i < model_->num_inputs () + model_->num_latches (); i ++)
		    {   //the value is DON'T CARE 
		        if (i >= model.size ())
		            break;
		        res.push_back (model[i]);
		    }
		    if (partial)
		    {
		        //TO BE DONE
		    }
		}
		else
		{
		    Assignment tmp;
		    tmp.resize (model_->num_latches (), 0);
		    for (int i = model_->num_inputs ()+1; i <= model_->num_inputs () + model_->num_latches (); i ++)
		    {
		    	
		    	int p = model_->prime (i);
		    	assert (p != 0);
		    	assert (model.size () > abs (p));
		    	
		    	int val = model[abs(p)-1];
		    	if (p == val)
		    		tmp[i-model_->num_inputs ()-1] = i;
		    	else
		    		tmp[i-model_->num_inputs ()-1] = -i;
		    }
		    
		    		    
		    for (int i = 0; i < tmp.size (); i ++)
		        res.push_back (tmp[i]);
		    if (partial)
		    {
		        //TO BE DONE
		    }
		}
		model = res;
	}
	
	void MainSolver::try_reduce (Cube& cu)
	{
		int try_times = int ((cu.size ()) * reduce_ratio_);
		int i = 0;
		int sz = cu.size ()-1;
		hash_set<int> tried;
		for (; i < try_times; i ++)
		{
			int pos = i;
			while (tried.find (cu[pos]) != tried.end ())
			{
				pos ++;
				if (pos >= cu.size ())
					return;
			}
			
		    assumption_.clear ();
		    for (int j = 0; j < pos; j ++)
		    {
			    assumption_push (cu[sz-j]);
		    }
		    for (int j = pos+1; j < cu.size (); j ++)
		    {
			    assumption_push (cu[sz-j]);
		    }
		    stats_->count_reduce_uc_SAT_time_start ();
		    if (!solve_with_assumption ())
		    {
		        cu = get_uc ();
		        try_times = int ((cu.size ()) * reduce_ratio_);
		        i = -1;
		        sz = cu.size ()-1;
		    }
		    else
		    	tried.insert (cu[pos]);
		    stats_->count_reduce_uc_SAT_time_end ();
		}
	}
	
	
}
