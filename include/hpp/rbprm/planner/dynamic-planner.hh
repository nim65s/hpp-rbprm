//
// Copyright (c) 2014 CNRS
// Authors: Florent Lamiraux
//
// This file is part of hpp-core
// hpp-core is free software: you can redistribute it
// and/or modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation, either version
// 3 of the License, or (at your option) any later version.
//
// hpp-core is distributed in the hope that it will be
// useful, but WITHOUT ANY WARRANTY; without even the implied warranty
// of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Lesser Public License for more details.  You should have
// received a copy of the GNU Lesser General Public License along with
// hpp-core  If not, see
// <http://www.gnu.org/licenses/>.

#ifndef HPP_RBPRM_DYNAMIC_PLANNER_HH
# define HPP_RBPRM_DYNAMIC_PLANNER_HH

# include <hpp/core/bi-rrt-planner.hh>


namespace hpp {
namespace rbprm {
/// \addtogroup path_planning
/// \{
  // forward declaration of class Planner
  HPP_PREDEF_CLASS (DynamicPlanner);
  // Planner objects are manipulated only via shared pointers
  typedef boost::shared_ptr <DynamicPlanner> DynamicPlannerPtr_t;

  using core::Problem;
  using core::Roadmap;
  using core::RoadmapPtr_t;
  using core::Path;
  using core::PathPtr_t;
  using core::Configuration_t;

/// Generic implementation of RRT algorithm
class DynamicPlanner : public core::BiRRTPlanner
{
public:
    /// Return shared pointer to new object.
    static DynamicPlannerPtr_t createWithRoadmap
    (const Problem& problem, const RoadmapPtr_t& roadmap);
    /// Return shared pointer to new object.
    static DynamicPlannerPtr_t create (const Problem& problem);
    /// One step of extension.
    virtual void oneStep ();
    /// Try to make direct connection between init and goal
    /// configurations, in order to avoid a random shoot.
    virtual void tryDirectPath();

    // we need both method, because smart_pointer inheritance is not implemented (compiler don't know that rbprmRoadmapPtr_t derive from RoadmapPtr_t).
    virtual const core::RoadmapPtr_t& roadmap () const{
      return roadmap_;
    }


protected:
    /// Constructor
    DynamicPlanner (const Problem& problem, const RoadmapPtr_t& roadmap);
    /// Constructor with roadmap
    DynamicPlanner (const Problem& problem);
    /// Store weak pointer to itself
    void init (const DynamicPlannerWkPtr_t& weak);

    /**
     * @brief computeGIWC compute the GIWC for the node configuration and fill the node attribut
     * @param x the node
     * @param report the RBPRM report corresponding to the node's configuration
     */
    void computeGIWC(const core::NodePtr_t x, core::ValidationReportPtr_t report);

    /**
     * @brief computeGIWC compute the GIWC for the node configuration and fill the node attribut, get validation report and call the second method
     * @param x the node
     */
    void computeGIWC(const core::NodePtr_t x);

    core::PathPtr_t extendInternal (core::ConfigurationPtr_t& qProj_, const core::NodePtr_t& near,
                    const core::ConfigurationPtr_t& target, bool reverse=false);

    /**
     * @brief setSteeringMethodBounds Compute the maximal acceleration on a direction from near to target,
     *                                and send it to the steering method
     * @param near the node from where we take the the information about contact and position
     * @param target the target configuration
     * @param reverse if true, we compute the acceleration from target to near, with the information from near
     */
    void setSteeringMethodBounds(const core::NodePtr_t& near, const core::ConfigurationPtr_t target,bool reverse);

private:




    core::ConfigurationPtr_t qProj_;
    DynamicPlannerWkPtr_t weakPtr_;
    const core::RoadmapPtr_t roadmap_;
    const core::steeringMethod::KinodynamicPtr_t sm_;
    const double mu = 0.5;
};
/// \}
} // namespace core
} // namespace hpp
#endif // HPP_RBPRM_DYNAMIC_PLANNER_HH
