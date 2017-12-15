// Copyright (c) 2014, LAAS-CNRS
// Authors: Steve Tonneau (steve.tonneau@laas.fr)
//
// This file is part of hpp-rbprm.
// hpp-rbprm is free software: you can redistribute it
// and/or modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation, either version
// 3 of the License, or (at your option) any later version.
//
// hpp-rbprm is distributed in the hope that it will be
// useful, but WITHOUT ANY WARRANTY; without even the implied warranty
// of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Lesser Public License for more details.  You should have
// received a copy of the GNU Lesser General Public License along with
// hpp-rbprm. If not, see <http://www.gnu.org/licenses/>.

#include <hpp/rbprm/rbprm-fullbody.hh>
#include <hpp/rbprm/interpolation/limb-rrt.hh>
#include <hpp/rbprm/interpolation/spline/effector-rrt.hh>
#include <hpp/rbprm/interpolation/com-rrt.hh>
#include <hpp/rbprm/tools.hh>
#include <hpp/model/joint.hh>
#include <hpp/core/bi-rrt-planner.hh>
#include <hpp/core/basic-configuration-shooter.hh>
#include <hpp/core/discretized-path-validation.hh>
#include <hpp/constraints/position.hh>
#include <bezier-com-traj/solve_end_effector.hh>

#include <spline/helpers/effector_spline.h>
#include <spline/bezier_curve.h>

namespace hpp {
using namespace core;
  namespace rbprm {
  namespace interpolation {

    typedef std::pair<value_type, vector_t> Waypoint;
    typedef std::vector<Waypoint, Eigen::aligned_allocator<Waypoint > > T_Waypoint;
    typedef T_Waypoint::iterator  IT_Waypoint;
    typedef T_Waypoint::const_iterator CIT_Waypoint;
    typedef Eigen::Matrix <value_type, 3, 1>   Vector3;
    const value_type epsilon = std::numeric_limits<value_type>::epsilon();


    bool Is2d(const T_Waypoint& wayPoints)
    {
        const std::size_t dim = wayPoints.front().second.rows() -1;
        const core::value_type ref = wayPoints.front().second[dim];
        value_type z_var = 0.;
        for(CIT_Waypoint cit = wayPoints.begin()+1 ; cit != wayPoints.end(); ++cit)
        {
            z_var = std::max(z_var, fabs(cit->second[dim] - ref));
        }
        return z_var < 0.02;
    }

    bool IsLine(const T_Waypoint& wayPoints)
    {
//return true;
        const vector_t& init  = wayPoints.front().second;
        // compute line between first and last
        vector_t dir =  wayPoints.back().second- init;
        vector_t dir2;
        dir.normalize();
        for(CIT_Waypoint cit = wayPoints.begin() + 1; cit != wayPoints.end()-1; ++cit)
        {
            dir2  = cit->second - init;
            if(dir.norm() > epsilon)
                dir2.normalize();
            else
                throw("todo waypoint distance nul in end efefctor interpolation");
            //if(dir.dot(dir2) < 0.9999)
            if(dir.dot(dir2) < 1 - epsilon *4)
                return false;
                //return false || Is2d(wayPoints);
        }
        return true;
    }

    Transform3f getEffectorTransformAt(core::DevicePtr_t device,const JointPtr_t effector,const core::PathPtr_t path,const value_type time){
        Configuration_t result(path->outputSize());
        (*path)(result,time);
        device->currentConfiguration(result);
        device->computeForwardKinematics();
        Transform3f transform = effector->currentTransformation();
        return transform;
    }

    void getEffectorConfigAt(core::DevicePtr_t device,const JointPtr_t effector,const core::PathPtr_t path,const value_type time,ConfigurationOut_t result ){
        Transform3f transform = getEffectorTransformAt(device,effector,path,time);
        result.head<3>() = transform.getTranslation();
        fcl::Quaternion3f quat = transform.getQuatRotation();
        result[3] = quat.getW();
        result[4] = quat.getX();
        result[5] = quat.getY();
        result[6] = quat.getZ();
    }

    vector_t GetEffectorPositionAt(core::PathPtr_t path, constraints::PositionPtr_t position, const value_type time)
    {
        vector_t result (position->outputSize());
        position->operator ()(result, path->operator ()(time));
        return result;
    }

    T_Waypoint getWayPoints(model::DevicePtr_t device, core::PathPtr_t path,
                         const JointPtr_t effector, const value_type effectorDistance, bool& isLine)
    {
        //create evaluation function
        constraints::PositionPtr_t position = createPositionMethod(device,fcl::Vec3f(), effector);
        // define arbitrary number of way points depending on length
        //std::size_t nbWayPoints = std::size_t(std::max(effectorDistance * 10, 3.));
        std::size_t nbWayPoints = 30;
        std::size_t dim = position->outputSize();
        if(!(nbWayPoints % 2)) nbWayPoints+=1;
        value_type pathIncrement = path->length() / nbWayPoints;
        T_Waypoint res;
        value_type t = path->timeRange().first;
        res.push_back(std::make_pair(0,GetEffectorPositionAt(path,position,t)));
        for(std::size_t i = 1; i <nbWayPoints-1; ++i)
        {
            t += pathIncrement;
            res.push_back(std::make_pair(i,GetEffectorPositionAt(path,position, t)));
        }
        res.push_back(std::make_pair(nbWayPoints-1,GetEffectorPositionAt(path,position,path->timeRange().second)));
        if(IsLine(res) && effectorDistance > 0.03 )
        {
            //value_type height = effectorDistance < 0.1 ? 0.01 : std::max(nbWayPoints* 0.015, 0.02) ;
//std::cout << "is line " << std::endl;
value_type height = effectorDistance < 0.1 ? 0.03 : std::min( 0.07, std::max(nbWayPoints* 0.01, 0.02)) ;
            isLine = true;
            T_Waypoint res2;
            res2.push_back(res.front());
            res2.push_back(std::make_pair(1, vector_t::Ones(position->outputSize()) * height +  res.front().second +  (res.back().second - res.front().second) / 2. ));
            res2.push_back(std::make_pair(2, res.back().second));
            return res2;
        }
        else if(Is2d(res) && effectorDistance > 0.03 )
        {
std::cout << "is 2d " << std::endl;
            isLine = true;
            std::size_t apex = nbWayPoints / 2;
            //value_type max_height = effectorDistance < 0.1 ? 0.01 : std::max(nbWayPoints* 0.015, 0.02);
value_type max_height = effectorDistance < 0.1 ? 0.03 : std::min( 0.07, std::max(nbWayPoints* 0.01, 0.02));
            value_type inc = max_height / apex;
            std::size_t current = 0;
            std::size_t inc_fac = 0;
            for(IT_Waypoint it = res.begin(); it != res.end()-1; ++it, ++current)
            {
                if(current>apex)
                {
                    it->second[dim-1] += inc_fac* (inc);
                    --inc_fac;
                }
                else
                {
                    it->second[dim-1] += inc_fac* inc;
                    if(current==apex)
                        --inc_fac;
                    else
                        ++inc_fac;
                }
            }
        }
        return res;
    }

    std::string getEffectorLimb(const  State &startState, const State &nextState)
    {
        return nextState.contactCreations(startState).front();
    }

    fcl::Vec3f getNormal(const std::string& effector, const State &state, bool& found)
    {
        std::map<std::string, fcl::Vec3f>::const_iterator cit = state.contactNormals_.find(effector);
        if(cit != state.contactNormals_.end())
        {
            found = true;
            return cit->second;
        }
        else
        {
            found = false;
            return fcl::Vec3f(0.,0.,1.);
        }
    }

    double genHeight(const bool normalFound)
    {
        if(normalFound)
            return 0.01;
        else
            return 0.;
    }


    JointPtr_t getEffector(RbPrmFullBodyPtr_t fullbody,
                           const  State &startState, const State &nextState)
    {
        std::string effectorVar = getEffectorLimb(startState, nextState);
        return fullbody->device_->getJointByName(fullbody->GetLimbs().at(effectorVar)->effector_->name());
    }

    exact_cubic_Ptr splineFromEffectorTraj(RbPrmFullBodyPtr_t fullbody, JointPtr_t effector, core::PathPtr_t path,
                                          const  State &startState, const State &nextState, bool& isLine)
    {
        // estimate length of distance travelled
        value_type length = effectorDistance(startState, nextState);
        T_Waypoint wayPoints = getWayPoints(fullbody->device_, path, effector, length, isLine);
        std::string effLimb = getEffectorLimb(startState,nextState);
        bool found (false);
        fcl::Vec3f n1 = getNormal(effLimb, startState, found);
        double h1 = genHeight(found);
        fcl::Vec3f n2 = getNormal(effLimb, nextState, found);
        double h2 = genHeight(found);
        std::cout << "AM I CALLED " << n1 << "\n" <<  n2 << "\n h1 " << h1 << "\n h2 " << h2 <<  std::endl;
       /* exact_cubic_Ptr ptr = exact_cubic_Ptr(spline::helpers::effector_spline(
                                                  wayPoints.begin(),
                                                  wayPoints.end(),
                                                  n1, n2,
                                                  h1, h2,
                                                  h1, h2));
                                                  */
        //exact_cubic_Ptr ptr = exact_cubic_Ptr(new spline_deriv_constraint_t(wayPoints.begin(), wayPoints.end()));
        exact_cubic_Ptr ptr = exact_cubic_Ptr(new exact_cubic_t(wayPoints.begin(), wayPoints.end()));
        isLine = true;
        //exact_cubic_Ptr ptr = exact_cubic_Ptr(new exact_cubic_t(wayPoints.begin(), wayPoints.end()));
        return ptr;
    }

    std::vector<model::JointPtr_t> getJointsByName(RbPrmFullBodyPtr_t fullbody, const std::vector<std::string>& names)
    {
        std::vector<model::JointPtr_t> res;
        for(std::vector<std::string>::const_iterator cit = names.begin(); cit != names.end(); ++cit)
        {
            res.push_back(fullbody->device_->getJointByName(*cit));
        }
        return res;
    }

    core::PathPtr_t effectorRRTFromPath(RbPrmFullBodyPtr_t fullbody, core::ProblemPtr_t referenceProblem, const PathPtr_t comPath,
                           const State &startState, const State &nextState,
                           const std::size_t numOptimizations, const bool keepExtraDof,
                           const PathPtr_t refPath, const std::vector<std::string>& constrainedJointPos,
                           const std::vector<std::string>& constrainedLockedJoints)
    {
        core::PathPtr_t fullBodyComPath = comRRT(fullbody, referenceProblem, comPath, startState, nextState, numOptimizations, true);
        //removing extra dof
        core::SizeInterval_t interval(0, fullBodyComPath->initial().rows()-1);
        core::SizeIntervals_t intervals;
        intervals.push_back(interval);
        core::PathPtr_t reducedComPath = core::SubchainPath::create(fullBodyComPath,intervals);
        if(effectorDistance(startState, nextState) < 0.03)
            return fullBodyComPath;
        JointPtr_t effector =  getEffector(fullbody, startState, nextState);
        EndEffectorPath endEffPath(fullbody->device_,effector,fullBodyComPath);
        // create a 'device' object for the end effector (freeflyer 6D). Needed for the path and the orientation constraint
        DevicePtr_t endEffectorDevice = Device::create("endEffector");
        JointPtr_t transJoint = new JointTranslation <3> (Transform3f());
        JointPtr_t so3Joint = new JointSO3(Transform3f());
        endEffectorDevice->rootJoint(transJoint);
        transJoint->addChildJoint (so3Joint);
        Configuration_t initConfig(endEffectorDevice->configSize()),endConfig(endEffectorDevice->configSize());
        getEffectorConfigAt(fullbody->device_,effector,fullBodyComPath,0,initConfig);
        getEffectorConfigAt(fullbody->device_,effector,fullBodyComPath,fullBodyComPath->length(),endConfig);
        Configuration_t takeoffConfig(initConfig),landingConfig(endConfig);
        // compute initial takeoff phase for the end effector :
        bezier_com_traj::ProblemData pDataTakeoff;
        pDataTakeoff.c0_=endEffPath(0);
        pDataTakeoff.c1_=endEffPath(0);
        pDataTakeoff.c1_[2]+=0.02; // TODO : replace with 0.05*normal, how can we retrieve the normal here ?
        pDataTakeoff.dc0_=Vector3::Zero();
        pDataTakeoff.ddc0_=Vector3::Zero();
        pDataTakeoff.dc1_=Vector3(0,0,0.2); // TODO replace with value * normal
        pDataTakeoff.ddc1_=Vector3::Zero();
        takeoffConfig.head<3>()=pDataTakeoff.c1_;
        hppDout(notice,"CREATE BEZIER for constraints : ");
        hppDout(notice,"c0   = "<<pDataTakeoff.c0_.transpose());
        hppDout(notice,"dc0  = "<<pDataTakeoff.dc0_.transpose());
        hppDout(notice,"ddc0 = "<<pDataTakeoff.ddc0_.transpose());
        hppDout(notice,"c1   = "<<pDataTakeoff.c1_.transpose());
        hppDout(notice,"dc1  = "<<pDataTakeoff.dc1_.transpose());
        hppDout(notice,"ddc1 = "<<pDataTakeoff.ddc1_.transpose());
        double timeTakeoff = 0.2; //TODO ??
        std::vector<bezier_t::point_t> pts;
        hppDout(notice,"Compute waypoints for takeOff phase : ");
        pts = bezier_com_traj::computeConstantWaypoints(pDataTakeoff,timeTakeoff,5);
        hppDout(notice,"Done.");
        std::ostringstream ssTakeoff;
        ssTakeoff<<"[";
        for(std::vector<bezier_t::point_t>::const_iterator wpit = pts.begin(); wpit != pts.end() ; ++wpit){
            ssTakeoff<<"["<<(*wpit)[0]<<","<<(*wpit)[1]<<","<<(*wpit)[2]<<"],";
        }
        ssTakeoff.seekp(-1,ssTakeoff.cur); ssTakeoff << ']';
        hppDout(notice,"Waypoint for reference end effector takeoff : ");
        hppDout(notice,ssTakeoff.str());
        bezier_t curve(pts.begin(),pts.end(),timeTakeoff);
        hppDout(notice,"Curve created, degree : "<<curve.degree_);
        BezierPathPtr_t refEffectorTakeoff = BezierPath::create(endEffectorDevice,pts.begin(),pts.end(),initConfig,takeoffConfig,core::interval_t(0.,timeTakeoff));
        hppDout(notice,"Path Bezier created");


        // compute final landing phase for the end effector :
        bezier_com_traj::ProblemData pDataLanding;
        pDataLanding.c0_=endEffPath(1);
        pDataLanding.c1_=endEffPath(1);
        pDataLanding.c0_[2]+=0.02; // TODO : replace with 0.05*normal, how can we retrieve the normal here ?
        pDataLanding.dc1_=Vector3::Zero();
        pDataLanding.ddc1_=Vector3::Zero();
        pDataLanding.dc0_=Vector3(0,0,-0.2); // TODO replace with value * normal
        pDataLanding.ddc0_=Vector3::Zero();
        landingConfig.head<3>()=pDataLanding.c0_;
        hppDout(notice,"CREATE BEZIER for constraints : ");
        hppDout(notice,"c0   = "<<pDataLanding.c0_.transpose());
        hppDout(notice,"dc0  = "<<pDataLanding.dc0_.transpose());
        hppDout(notice,"ddc0 = "<<pDataLanding.ddc0_.transpose());
        hppDout(notice,"c1   = "<<pDataLanding.c1_.transpose());
        hppDout(notice,"dc1  = "<<pDataLanding.dc1_.transpose());
        hppDout(notice,"ddc1 = "<<pDataLanding.ddc1_.transpose());
        double timeLanding = 0.2; //TODO ??
        hppDout(notice,"Compute waypoints for landing phase : ");
        pts = bezier_com_traj::computeConstantWaypoints(pDataLanding,timeLanding,5);
        hppDout(notice,"Done.");
        BezierPathPtr_t refEffectorLanding = BezierPath::create(endEffectorDevice,pts.begin(),pts.end(),landingConfig,endConfig,core::interval_t(0.,timeLanding));
        std::ostringstream ssLanding;
        ssLanding<<"[";
        for(std::vector<bezier_t::point_t>::const_iterator wpit = pts.begin(); wpit != pts.end() ; ++wpit){
            ssLanding<<"["<<(*wpit)[0]<<","<<(*wpit)[1]<<","<<(*wpit)[2]<<"],";
        }
        ssLanding.seekp(-1,ssLanding.cur); ssLanding << ']';
        hppDout(notice,"Waypoint for reference end effector landing : ");
        hppDout(notice,ssLanding.str());


        // compute bezier curve that follow the rrt path and that respect the constraints :
        bezier_com_traj::ProblemData pDataMid;
        pDataMid.c0_=pDataTakeoff.c1_;
        pDataMid.c1_=pDataLanding.c0_;
        pDataMid.dc0_=pDataTakeoff.dc1_;
        pDataMid.dc1_=pDataLanding.dc0_;
        pDataMid.ddc0_=pDataTakeoff.ddc1_;
        pDataMid.ddc1_=pDataLanding.ddc0_;
        double timeMid = fullBodyComPath->length() - timeLanding - timeTakeoff;

        hppDout(notice,"CREATE BEZIER for constraints : ");
        hppDout(notice,"c0   = "<<pDataMid.c0_.transpose());
        hppDout(notice,"dc0  = "<<pDataMid.dc0_.transpose());
        hppDout(notice,"ddc0 = "<<pDataMid.ddc0_.transpose());
        hppDout(notice,"c1   = "<<pDataMid.c1_.transpose());
        hppDout(notice,"dc1  = "<<pDataMid.dc1_.transpose());
        hppDout(notice,"ddc1 = "<<pDataMid.ddc1_.transpose());

        hppDout(notice,"Distance traveled by the end effector : "<<(pDataMid.c1_-pDataMid.c0_).norm());
        hppDout(notice,"Distance : "<<(pDataMid.c1_-pDataMid.c0_).transpose());
        hppDout(notice,"Time = "<<timeMid);
        bezier_com_traj::ResultDataCOMTraj res = bezier_com_traj::solveEndEffector<EndEffectorPath>(pDataMid,endEffPath,timeMid,0.);
        if(!res.success_){
            hppDout(warning,"[WARNING] qp solver failed to compute bezier curve !!");
            return fullBodyComPath;
        }
        bezier_Ptr refEffector=bezier_Ptr(new bezier_t(res.c_of_t_));
        bezier_t::t_point_t wps = refEffector->waypoints();
        std::ostringstream ss;
        ss<<"[";
        for(bezier_t::cit_point_t wpit = wps.begin() ; wpit != wps.end() ; ++wpit){
            ss<<"["<<(*wpit)[0]<<","<<(*wpit)[1]<<","<<(*wpit)[2]<<"],";
        }
        ss.seekp(-1,ss.cur); ss << ']';
        hppDout(notice,"Waypoint for reference end effector : ");
        hppDout(notice,ss.str());

        BezierPathPtr_t refEffectorMid = BezierPath::create(endEffectorDevice,refEffector,takeoffConfig,landingConfig,core::interval_t(0.,timeMid));

        // merge the 3 curves :
        PathVectorPtr_t refEffectorPath  = PathVector::create (refEffectorMid->outputSize (),refEffectorMid->outputDerivativeSize ());
        refEffectorPath->appendPath(refEffectorTakeoff);
        refEffectorPath->appendPath(refEffectorMid);
        refEffectorPath->appendPath(refEffectorLanding);

        //return fullBodyComPath;//TEST
        EffectorRRTShooterFactory shooterFactory(reducedComPath);
        std::vector<model::JointPtr_t> constrainedJoint = getJointsByName(fullbody, constrainedJointPos);
        std::vector<model::JointPtr_t> constrainedLocked = getJointsByName(fullbody, constrainedLockedJoints);
        hppDout(notice,"effectorRRT, contrained joint pose size : "<<constrainedJointPos.size());
        hppDout(notice,"effectorRRT, contrained locked joint  size : "<<constrainedLockedJoints.size());

        SetEffectorRRTConstraints constraintFactory(comPath, refEffectorPath, refPath, effector, constrainedJoint, constrainedLocked);
        T_StateFrame stateFrames;
        stateFrames.push_back(std::make_pair(comPath->timeRange().first, startState));
        stateFrames.push_back(std::make_pair(comPath->timeRange().second, nextState));
        return interpolateStatesFromPath<EffectorRRTHelper, EffectorRRTShooterFactory, SetEffectorRRTConstraints>
                (fullbody, referenceProblem, shooterFactory, constraintFactory, comPath,
                 //stateFrames.begin(), stateFrames.begin()+1, numOptimizations % 10, keepExtraDof);
                 stateFrames.begin(), stateFrames.begin()+1, numOptimizations, keepExtraDof, 0.0001);
    }

    core::PathPtr_t effectorRRT(RbPrmFullBodyPtr_t fullbody, core::ProblemPtr_t referenceProblem, const PathPtr_t comPath,
                           const  State &startState, const State &nextState,
                           const  std::size_t numOptimizations,
                           const bool keepExtraDof)
    {
        const std::vector<std::string> dum, dum2;
        return effectorRRTFromPath(fullbody, referenceProblem, comPath, startState, nextState, numOptimizations, keepExtraDof, core::PathPtr_t(), dum, dum2);
    }


    core::PathPtr_t effectorRRT(RbPrmFullBodyPtr_t fullbody, core::ProblemPtr_t referenceProblem, const PathPtr_t comPath,
                           const  State &startState, const State &nextState,
                           const  std::size_t numOptimizations,
                           const bool keepExtraDof, const std::vector<std::string>& constrainedJointPos, const std::vector<std::string>& constrainedLockedJoints)
    {
        return effectorRRTFromPath(fullbody, referenceProblem, comPath, startState, nextState, numOptimizations, keepExtraDof, core::PathPtr_t(), constrainedJointPos, constrainedLockedJoints);
    }

    void SetEffectorRRTConstraints::operator ()(EffectorRRTHelper& helper, const State& from, const State& to) const
    {
        CreateContactConstraints<EffectorRRTHelper>(helper, from, to);
        CreateComConstraint<EffectorRRTHelper,core::PathPtr_t >(helper, refCom_);
        CreateEffectorConstraint<EffectorRRTHelper,core::PathPtr_t >(helper, refEff_, effector_);
       /* Configuration_t refConfig = helper.fullbody_->referenceConfig();
        CreatePosturalTaskConstraint<EffectorRRTHelper,Configuration_t>(helper, refConfig);
        helper.proj_->lastIsOptional(true);
        helper.proj_->numOptimize(500);
        helper.proj_->lastAsCost(true);
        helper.proj_->errorThreshold(1e-3);*/
        if(refFullbody_)
        {
            hppDout(notice,"Ref fullBody provided, create 6D effector constraint : ");
            for(std::vector<model::JointPtr_t>::const_iterator cit = constrainedJointPos_.begin();
                cit != constrainedJointPos_.end(); ++cit)
            {
                hppDout(notice,"Constrained joint pose : "<<(*cit)->name());
                Create6DEffectorConstraint<EffectorRRTHelper, core::PathPtr_t  >(helper, refFullbody_, *cit);
            }
        }
    }

    vector_t EndEffectorPath::operator ()(double t) const{
        double u = fullBodyPath_->timeRange().first + t*fullBodyPath_->length(); // t is between 0 and 1
        vector_t res = GetEffectorPositionAt(fullBodyPath_,positionConstraint_,u);
        return Vector3(res);
    }


  }// namespace interpolation
  }// namespace rbprm
}// namespace hpp
