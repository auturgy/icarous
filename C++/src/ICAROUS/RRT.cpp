/**
 * RRT
 *
 * RRT search
 *
 * Contact: Swee Balachandran (swee.balachandran@nianet.org)
 *
 *
 * Copyright (c) 2011-2016 United States Government as represented by
 * the National Aeronautics and Space Administration.  No copyright
 * is claimed in the United States under Title 17, U.S.Code. All Other
 * Rights Reserved.
 *
 * Notices:
 *  Copyright 2016 United States Government as represented by the Administrator of the National Aeronautics and Space Administration.
 *  All rights reserved.
 *
 * Disclaimers:
 *  No Warranty: THE SUBJECT SOFTWARE IS PROVIDED "AS IS" WITHOUT ANY WARRANTY OF ANY KIND, EITHER EXPRESSED,
 *  IMPLIED, OR STATUTORY, INCLUDING, BUT NOT LIMITED TO, ANY WARRANTY THAT THE SUBJECT SOFTWARE WILL CONFORM TO SPECIFICATIONS, ANY
 *  IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, OR FREEDOM FROM INFRINGEMENT,
 *  ANY WARRANTY THAT THE SUBJECT SOFTWARE WILL BE ERROR FREE, OR ANY WARRANTY THAT DOCUMENTATION, IF PROVIDED,
 *  WILL CONFORM TO THE SUBJECT SOFTWARE. THIS AGREEMENT DOES NOT, IN ANY MANNER, CONSTITUTE AN ENDORSEMENT BY GOVERNMENT
 *  AGENCY OR ANY PRIOR RECIPIENT OF ANY RESULTS, RESULTING DESIGNS, HARDWARE, SOFTWARE PRODUCTS OR ANY OTHER APPLICATIONS
 *  RESULTING FROM USE OF THE SUBJECT SOFTWARE.  FURTHER, GOVERNMENT AGENCY DISCLAIMS ALL WARRANTIES AND
 *  LIABILITIES REGARDING THIRD-PARTY SOFTWARE, IF PRESENT IN THE ORIGINAL SOFTWARE, AND DISTRIBUTES IT "AS IS."
 *
 * Waiver and Indemnity:
 *   RECIPIENT AGREES TO WAIVE ANY AND ALL CLAIMS AGAINST THE UNITED STATES GOVERNMENT,
 *   ITS CONTRACTORS AND SUBCONTRACTORS, AS WELL AS ANY PRIOR RECIPIENT.  IF RECIPIENT'S USE OF THE SUBJECT SOFTWARE
 *   RESULTS IN ANY LIABILITIES, DEMANDS, DAMAGES,s
 *   EXPENSES OR LOSSES ARISING FROM SUCH USE, INCLUDING ANY DAMAGES FROM PRODUCTS BASED ON, OR RESULTING FROM,
 *   RECIPIENT'S USE OF THE SUBJECT SOFTWARE, RECIPIENT SHALL INDEMNIFY AND HOLD HARMLESS THE UNITED STATES GOVERNMENT,
 *   ITS CONTRACTORS AND SUBCONTRACTORS, AS WELL AS ANY PRIOR RECIPIENT, TO THE EXTENT PERMITTED BY LAW.
 *   RECIPIENT'S SOLE REMEDY FOR ANY SUCH MATTER SHALL BE THE IMMEDIATE, UNILATERAL TERMINATION OF THIS AGREEMENT.
 */

#include "RRT.h"
#include <iostream>
#include <fstream>
#include "ParameterData.h"
#include "SeparatedInput.h"

RRT_t::RRT_t(){
	nodeCount = 0;
	trafficSize = 0;
	xmin = 0;
	xmax = 0;
	ymin = 0;
	ymax = 0;
	zmin = 0;
	zmax = 0;
	Tstep = 5;
	maxInputNorm = 1;
	dT = 1;
	closestDist = MAXDOUBLE;
	if(!DAA.parameters.loadFromFile("params/DaidalusQuadConfig.txt")){
		printf("error:no file found\n");
	}
	daaLookAhead = DAA.parameters.getLookaheadTime("s");
	goalreached = false;
	time(&startTime);
	maxD = DAA.parameters.alertor.getLevel(1).getDetectorRef()->getParameters().getValue("D");
}

RRT_t::RRT_t(std::list<Geofence_t> &fenceList,Position initialPos,Velocity initialVel,
		std::vector<Position> trafficPos,std::vector<Velocity> trafficVel,int stepT,double dt,double inputNormMax){

	Tstep = stepT;
	dT    = dt;
	std::list<Geofence_t>::iterator it;

	proj = Projection::createProjection(initialPos.mkAlt(0));

	for(it = fenceList.begin();it != fenceList.end(); ++it){
		if(it->GetType() == KEEP_IN){
			boundingBox = it->GetPoly().poly3D(proj);
			break;
		}
	}

	xmin = -100;
	xmax = 100;
	ymin = -100;
	ymax = 100;
	zmin = -1;
	zmax = 10;

	maxInputNorm = inputNormMax;

	goalreached = false;

	for(it = fenceList.begin();it != fenceList.end(); ++it){
		if(it->GetType() != KEEP_IN){
			obstacleList.push_back((it->GetPoly().poly3D(proj)));
		}
	}

	Vect3 initPosR3 = proj.project(initialPos);

	nodeCount = 0;
	root.id = nodeCount;
	root.pos = initPosR3;
	root.vel = Vect3(initialVel.x,initialVel.y,initialVel.z);

	std::vector<Position>::iterator itP;
	std::vector<Velocity>::iterator itV;
	for(itP = trafficPos.begin(),itV = trafficVel.begin();
		itP != trafficPos.end() && itP != trafficPos.end();
		++itP, ++itV){
		Vect3 tPos = proj.project(*itP);
		Vect3 tVel = Vect3(itV->x,itV->y,itV->z);
		root.trafficPos.push_back(tPos);
		root.trafficVel.push_back(tVel);
	}
	root.parent = NULL;

	trafficSize = trafficPos.size();
	nodeList.push_back(root);

	closestDist = MAXDOUBLE;
	closestNode = root;

	DAA.parameters.loadFromFile("params/DaidalusQuadConfig.txt");
	daaLookAhead = DAA.parameters.getLookaheadTime("s");
	time(&startTime);
	maxD = DAA.parameters.alertor.getLevel(1).getDetectorRef()->getParameters().getValue("D");
	//maxD = maxD;
}

void RRT_t::Initialize(Vect3 Pos,Vect3 Vel,
						std::vector<Vect3> TrafficPos,std::vector<Vect3> TrafficVel){
	root.pos = Pos;
	root.vel = Vel;
	root.trafficPos = TrafficPos;
	root.trafficVel = TrafficVel;
	root.id = nodeCount;
	root.parent = NULL;

	nodeList.push_back(root);
	trafficSize = TrafficPos.size();

	xmin = 0;
	xmax = 100;
	ymin = 0;
	ymax = 100;
	zmin = 0;
	zmax = 100;
}

double RRT_t::NodeDistance(node_t A, node_t B){
	return sqrt(pow((A.pos.x - B.pos.x),2) + pow((A.pos.y - B.pos.y),2));
}

void RRT_t::GetInput(node_t nn, node_t qn,double U[]){
	double dx, dy, dz;
	double norm;

	dx = qn.pos.x - nn.pos.x;
	dy = qn.pos.y - nn.pos.y;
	dz = qn.pos.z - nn.pos.z;

	norm = sqrt(pow(dx,2) + pow(dy,2) + pow(dz,2));

	if (norm > maxInputNorm){
		U[0] = dx/norm * maxInputNorm;
		U[1] = dy/norm * maxInputNorm;
		U[2] = dz/norm * maxInputNorm;
	}
	else{
		U[0] = dx;
		U[1] = dy;
		U[2] = dz;
	}
}

node_t* RRT_t::FindNearest(node_t query){
	double minDist = 10000;
	double dist;
	node_t* nearest;

	for(ndit = nodeList.begin();ndit != nodeList.end(); ++ndit){
		dist = NodeDistance(*ndit,query);

		if(dist < minDist){
			minDist = dist;
			nearest = &(*ndit);
		}
	}


	return nearest;
}

void RRT_t::F(double X[], double U[],double Y[]){

	double Kc = 0.3;

	Y[0] = X[1];
	Y[1] = -Kc*(X[1] - U[0]);
	Y[2] = X[3];
	Y[3] = -Kc*(X[3] - U[1]);
	Y[4] = X[5];
	Y[5] = -Kc*(X[5] - U[2]);

	// Constant velocity for traffic
	for(int i=0;i<trafficSize;++i){
		Y[6+(6*i)+0]   = X[6+(6*i)+1];
		Y[6+(6*i)+1]   = 0;
		Y[6+(6*i)+2]   = X[6+(6*i)+3];
		Y[6+(6*i)+3]   = 0;
		Y[6+(6*i)+4]   = X[6+(6*i)+5];
		Y[6+(6*i)+5]   = 0;
	}
}

node_t RRT_t::MotionModel(node_t nearest, double U[]){

	Vect3 pos = nearest.pos;
	Vect3 vel = nearest.vel;
	std::vector<Vect3> trafficPos = nearest.trafficPos;
	std::vector<Vect3> trafficVel = nearest.trafficVel;

	int Xsize = 6+trafficSize*6;

	double *X   = new double[Xsize];
	double *X_p = new double[Xsize];
	double *Y   = new double[Xsize];
	double *k1  = new double[Xsize];
	double *k2  = new double[Xsize];

	memset(Y,0,sizeof(double)*Xsize);
	memset(k1,0,sizeof(double)*Xsize);
	memset(k2,0,sizeof(double)*Xsize);

	X[0] = pos.x;
	X[1] = vel.x;
	X[2] = pos.y;
	X[3] = vel.y;
	X[4] = pos.z;
	X[5] = vel.z;

	std::vector<Vect3>::iterator vecItP;
	std::vector<Vect3>::iterator vecItV;
	int i=0;
	for(vecItP = trafficPos.begin(),vecItV = trafficVel.begin();
		vecItP != trafficPos.end() && vecItV != trafficVel.end();
		++vecItP,++vecItV){
		X[6+(6*i)+0] =  vecItP->x;
		X[6+(6*i)+1] =  vecItV->x;
		X[6+(6*i)+2] =  vecItP->y;
		X[6+(6*i)+3] =  vecItV->y;
		X[6+(6*i)+4] =  vecItP->z;
		X[6+(6*i)+5] =  vecItV->z;
		i++;
	}

	Vect3 newPos;
	Vect3 newVel;
	std::vector<Vect3> newTrafficPos;
	bool fenceConflict = false;
	bool trafficConflict = false;

	for(int i=0;i<Tstep;++i){
		F(X,U,Y);

		for(int j=0;j<Xsize;j++){
			k1[j] = Y[j]*dT;
			X_p[j] = X[j] + k1[j];
		}

		F(X_p,U,Y);
		for(int j=0;j<Xsize;j++){
			k2[j] = Y[j]*dT;
		}

		for(int j=0;j<Xsize;j++){
			X[j] = X[j] + 0.5*(k1[j] + k2[j]);
		}

		newPos.x = X[0]; newPos.y = X[2]; newPos.z = X[4];
		newVel.x = X[1]; newVel.y = X[3]; newVel.z = X[5];

		if(CheckFenceCollision(newPos)){
			fenceConflict = true;
		}

		newTrafficPos.clear();
		for(int j=0;j<trafficSize;++j){
			Vect3 newTraffic(X[6+(6*j)+0],X[6+(6*j)+2],X[6+(6*j)+4]);
			newTrafficPos.push_back(newTraffic);
		}

		if(trafficSize > 0){
			trafficConflict = CheckTrafficCollision(newPos,newVel,newTrafficPos,trafficVel);
		}

		if(fenceConflict || trafficConflict){
			node_t newNode;
			newNode.id = -1;
			return newNode;
		}
	}

	/*
	if(trafficSize > 0){
		bool checkTurn;
		if(nearest.id > 0){
			checkTurn = true;
		}
		else{
			checkTurn = false;
		}
		if(CheckTrafficCollisionWithBands(checkTurn,newPos,newVel,newTrafficPos,trafficVel,vel)){
			node_t newNode;
			newNode.id = -1;
			return newNode;
		}
	}*/


	node_t newNode;
	nodeCount++;
	newNode.id  = nodeCount;
	newNode.pos = newPos;
	newNode.vel = newVel;
	newNode.trafficPos = newTrafficPos;
	newNode.trafficVel = trafficVel;
	newNode.waitTime = 0.0;

	delete[] X;
	delete[] X_p;
	delete[] Y;
	delete[] k1;
	delete[] k2;
	return newNode;
}



void RRT_t::RRTStep(int i){

	double X[2];
	// Generate random number
	int rangeX = xmax - xmin;
	int rangeY = ymax - ymin;

	X[0] = xmin + (rand() % rangeX);
	X[1] = ymin + (rand() % rangeY);


	node_t rd;
	rd.pos.x = X[0];
	rd.pos.y = X[1];
	rd.pos.z = root.pos.z;    // should  be set appropriately for 3D plans

	node_t *nearest = FindNearest(rd);
	node_t newNode;

	double U[3];
	GetInput(*nearest,rd,U);

	if(CheckDirectPath2Goal(*nearest)){
		newNode = goalNode;
		nodeCount++;
		newNode.id = nodeCount;
	}else{
		newNode = MotionModel(*nearest,U);

		if(newNode.id < 0){
			return;
		}
	}


	nearest->children.push_back(newNode);
	newNode.parent = nearest;
	nodeList.push_back(newNode);

}

bool RRT_t::CheckFenceCollision(Vect3 qPos){
	std::list<Poly3D>::iterator it;
	for(it = obstacleList.begin();it != obstacleList.end(); ++it){
		if(geoPolycarp.definitelyInside(qPos,*it)){
			return true;
		}
	}

	if(boundingBox.size() > 2){
		if(!geoPolycarp.definitelyInside(qPos,boundingBox)){
			return true;
		}
	}
	return false;
}

bool RRT_t::CheckProjectedFenceConflict(node_t qnode,node_t goal){
	std::list<Poly3D>::iterator it;
	for(it = obstacleList.begin();it != obstacleList.end(); ++it){
		int sizePoly = it->size();
		for(int i=0;i<sizePoly;i++){
			int j;
			if(i == sizePoly - 1){
				j = 0;
			}
			else{
				j = i+1;
			}


			bool intCheck = LinePlanIntersection(it->getVertex(i),it->getVertex(j),
									  	         it->getBottom(),it->getTop(),
												 qnode.pos,goal.pos);

			if(intCheck){
				return true;
			}
		}
	}

	return false;
}

bool RRT_t::CheckTrafficCollision(Vect3 qPos,Vect3 qVel,std::vector<Vect3> TrafficPos,std::vector<Vect3> TrafficVel){
	time_t currentTime;
	time(&currentTime);
	double elapsedTime = difftime(currentTime,startTime);

	Position so  = Position::makeXYZ(qPos.x,"m",qPos.y,"m",qPos.z,"m");
	Velocity vo  = Velocity::makeVxyz(qVel.x,qVel.y,"m/s",qVel.z,"m/s");

	DAA.setOwnshipState("Ownship",so,vo,elapsedTime);


	std::vector<Vect3>::iterator itP;
	std::vector<Vect3>::iterator itV;
	int i=0;
	double trafficDist = MAXDOUBLE;
	double violationTime;
	double alertTime = DAA.parameters.alertor.getLevel(1).getAlertingTime();
	for(itP = TrafficPos.begin(),itV = TrafficVel.begin();
			itP != TrafficPos.end() && itV != TrafficVel.end();
			++itP,++itV){
		Position si = Position::makeXYZ(itP->x,"m",itP->y,"m",itP->z,"m");
		Velocity vi = Velocity::makeVxyz(itV->x,itV->y,"m/s",itV->z,"m/s");
		char name[10];
		sprintf(name,"Traffic%d",i);i++;
		DAA.addTrafficState(name,si,vi);

		//printf("Traffic pos:%f,%f\n",itP->x,itP->y);
		//printf("Traffic heading:%f\n",vi.track("degree"));

		double distH = so.distanceH(si);

		if(distH < trafficDist){
			trafficDist = distH;
		}

		DAA.kinematicMultiBands(KMB);
		if(!KMB.timeIntervalOfViolation(1).isEmpty()){
			violationTime = KMB.timeIntervalOfViolation(1).low;
		}
		else{
			violationTime = NAN;
		}

		if(ISFINITE(violationTime) && violationTime <= alertTime){
			return true;
		}
	}

	return false;
}

bool RRT_t::CheckTrafficCollisionWithBands(bool CheckTurn,Vect3 qPos,Vect3 qVel,
	    std::vector<Vect3> TrafficPos,std::vector<Vect3> TrafficVel,Vect3 oldVel){


	time_t currentTime;
	time(&currentTime);
	double elapsedTime = difftime(currentTime,startTime);

	Position so  = Position::makeXYZ(qPos.x,"m",qPos.y,"m",qPos.z,"m");
	Velocity vo  = Velocity::makeVxyz(qVel.x,qVel.y,"m/s",qVel.z,"m/s");

	DAA.setOwnshipState("Ownship",so,vo,elapsedTime);

	std::vector<Vect3>::iterator itP;
	std::vector<Vect3>::iterator itV;
	int i=0;
	double trafficDist = MAXDOUBLE;

	for(itP = TrafficPos.begin(),itV = TrafficVel.begin();
		itP != TrafficPos.end() && itV != TrafficVel.end();
		++itP,++itV){
		Position si = Position::makeXYZ(itP->x,"m",itP->y,"m",itP->z,"m");
		Velocity vi = Velocity::makeVxyz(itV->x,itV->y,"m/s",itV->z,"m/s");
		char name[10];
		sprintf(name,"Traffic%d",i);i++;
		DAA.addTrafficState(name,si,vi);

		//printf("Traffic pos:%f,%f\n",itP->x,itP->y);
		//printf("Traffic heading:%f\n",vi.track("degree"));

		double distH = so.distanceH(si);

		if(distH < trafficDist){
			trafficDist = distH;
		}
	}

	//TODO: change this number to a parameter
	if(trafficDist < maxD){
		printf("In cylinder\n");
		return true;
	}

	double qHeading = vo.track("degree");


	//printf("curr heading:%f\n",qHeading);
	//printf("old heading:%f\n",oldHeading);

	DAA.kinematicMultiBands(KMB);
	if( BandsRegion::isConflictBand(KMB.regionOfTrack(DAA.getOwnshipState().track()))){
		return true;
	}
	else if(CheckTurn){
		Velocity vo0 = Velocity::makeVxyz(oldVel.x,oldVel.y,"m/s",oldVel.z,"m/s");
		double oldHeading = vo0.track("degree");
		if(BandsRegion::isConflictBand(KMB.regionOfTrack(vo0.trk()))){
			return true;
		}
		// Check collision with traffic based on current heading
		for(int ib=0;ib<KMB.trackLength();++ib){
			if(KMB.trackRegion(ib) != larcfm::BandsRegion::NONE ){
				Interval ii = KMB.track(ib,"deg");

				if(CheckTurnConflict(ii.low,ii.up,qHeading,oldHeading)){
					//printf("RRT:turn conflict old:%f, new:%f [%f,%f]\n",oldHeading,qHeading,ii.low,ii.up);
					return true;
				}
			}
		}
	}

	return false;
}

bool RRT_t::CheckTurnConflict(double low,double high,double newHeading,double oldHeading){

	if(newHeading < 0){
		newHeading = 360 + newHeading;
	}

	if(oldHeading < 0){
		oldHeading = 360 + oldHeading;
	}

	// Get direction of turn
	double psi   = newHeading - oldHeading;
	double psi_c = 360 - std::abs(psi);
	bool rightTurn = false;
	if(psi > 0){
		if(std::abs(psi) > std::abs(psi_c)){
			rightTurn = false;
		}
		else{
			rightTurn = true;
		}
	}else{
		if(std::abs(psi) > std::abs(psi_c)){
			rightTurn = true;
		}
		else{
			rightTurn = false;
		}
	}

	double A,B,X,Y,diff;
	if(rightTurn){
		diff = oldHeading;
		A = oldHeading - diff;
		B = newHeading - diff;
		X = low - diff;
		Y = high - diff;

		if(B < 0){
			B = 360 + B;
		}

		if(X < 0){
			X = 360 + X;
		}

		if(Y < 0){
			Y = 360 + Y;
		}

		if(A < X && B > Y){
			return true;
		}
	}else{
		diff = 360 - oldHeading;
		A    = oldHeading + diff;
		B    = newHeading + diff;
		X = low + diff;
		Y = high + diff;

		if(B > 360){
			B = B - 360;
		}

		if(X > 360){
			X = X - 360;
		}

		if(Y > 360){
			Y = Y - 360;
		}

		if(A > Y && B < X){
			return true;
		}
	}

	return false;
}



bool RRT_t::CheckDirectPath2Goal(node_t qnode){

	Vect3 A = qnode.pos;
	Vect3 B = goalNode.pos;
	Vect3 AB = B.Sub(A);
	double norm = AB.norm();
	if(norm > 0){
		AB = AB.Scal(maxInputNorm/norm);
	}

	if(CheckProjectedFenceConflict(qnode,goalNode)){
		return false;
	}

	if(trafficSize > 0){

		Vect3 vel(0,0,0);
		if(qnode.parent != NULL){
			node_t *parent = qnode.parent;
			vel = parent->vel;
		}

		bool CheckTurn = false;
		if(CheckTrafficCollisionWithBands(CheckTurn,qnode.pos,AB,qnode.trafficPos,qnode.trafficVel,vel)){
				return false;
		}
		else{
			return true;
		}
	}
	else{
		return false;
	}
}

bool RRT_t::CheckWaitAndGo(node_t qnode){

	Vect3 pos = qnode.pos;
	Vect3 vel = qnode.vel;
	double speed = maxInputNorm;
	Vect3 A = qnode.pos;
	Vect3 B = goalNode.pos;
	Vect3 AB = B.Sub(A);
	AB = AB.mkZ(0);
	double norm = AB.norm();
	if(norm > 0){
		AB = AB.Scal(speed/norm);
	}

	std::vector<Vect3> trafficListPos = qnode.trafficPos;
	std::vector<Vect3> trafficListVel = qnode.trafficVel;
	std::vector<Vect3>::iterator itrP,itrV;
	double maxTime = 0;

	for(itrP=trafficListPos.begin(),itrV=trafficListVel.begin(); itrP != trafficListPos.end(); itrP++, itrV++){
		Vect3 st = *itrP;
		Vect3 vt = *itrV;
		Vect3 V  = vt.Sub(AB);
		Vect3 S  = pos.Sub(st);

		Vect3 xV = AB.cross(vt);

		// cross product of velocities are zero if aircraft heading in parallel directions.
		if(xV.norm() < 0.1){
			// If velocities are parallel
			double cosangle = AB.dot(S)/(AB.norm()*S.norm());
			double angle = acos(cosangle);

			if(angle > 90*M_PI/180){
				angle = M_PI - angle;
			}

			double perpdist = S.norm()*sin(angle);

			if (perpdist < 5){
				return false;
			}
		}

		if (V.norm() < 1e-2){
			return false;
		}
		double C = st.x * V.y - st.y*V.x;
		double E = vt.x*V.y - vt.y*V.x;

		if(E < 0.1){
			return false;
		}

		double D = 5; //TODO: remove this hard coded parameter
		double t0 = (-D* sqrt(V.dot(V)) - C)/E;
		double t1 = (D* sqrt(V.dot(V)) - C)/E;
		double t = 0;

		if (t0 > 0){
			t = t0;
		}
		else if(t1 > 0){
			t = t1;
		}

		if (t > maxTime){
			maxTime = t;
		}
	}

	if(maxTime > 0.0){
		qnode.waitTime = maxTime;
		printf("Wait time: %f\n",maxTime);
		return true;
	}
	else{
		return false;
	}

}

bool RRT_t::CheckGoal(){

	node_t lastNode = nodeList.back();

	Vect3 diff = lastNode.pos.Sub(goalNode.pos);
	double mag = diff.norm();

	if(mag <= closestDist){
		closestDist = mag;
		closestNode = lastNode;

		if(CheckDirectPath2Goal(closestNode)){
			printf("found direct path to goal\n");
			goalreached = true;
			return true;
		}
	}

	if( mag < 3 ){
		printf("found goal\n");
		goalreached = true;
		return true;
	}else{
		goalreached = false;
		return false;
	}
}

void RRT_t::SetGoal(Position goal){
	goalNode.pos = proj.project(goal);
}

void RRT_t::SetGoal(node_t goal){
	goalNode = goal;
}


bool RRT_t::LinePlanIntersection(Vect2 A,Vect2 B,double floor,double ceiling,Vect3 CurrPos,Vect3 NextWP){

	double x1 = A.x;
	double y1 = A.y;
	double z1 = floor;

	double x2 = B.x;
	double y2 = B.y;
	double z2 = ceiling;

	Vect3 l0 = Vect3(CurrPos.x, CurrPos.y, CurrPos.z);
	Vect3 p0 = Vect3(x1, y1, z1);

	Vect3 n = Vect3(-(z2 - z1) * (y2 - y1), (z2 - z1) * (x2 - x1), 0);
	Vect3 l = Vect3(NextWP.x - CurrPos.x, NextWP.y - CurrPos.y, NextWP.z - CurrPos.z);

	double d = (p0.Sub(l0).dot(n)) / (l.dot(n));

	Vect3 PntI = l0.Add(l.Scal(d));


	Vect3 OA = Vect3(x2 - x1, y2 - y1, 0);
	Vect3 OB = Vect3(0, 0, z2 - z1);
	Vect3 OP = PntI.Sub(p0);
	Vect3 CN = NextWP.Sub(CurrPos);
	Vect3 CP = PntI.Sub(CurrPos);

	double proj1 = OP.dot(OA) / pow(OA.norm(), 2);
	double proj2 = OP.dot(OB) / pow(OB.norm(), 2);
	double proj3 = CP.dot(CN) / pow(CN.norm(), 2);

	if (proj1 >= 0 && proj1 <= 1) {
		if (proj2 >= 0 && proj2 <= 1) {
			if (proj3 >= 0 && proj3 <= 1)
				return true;
		}
	}

	return false;
}

Plan RRT_t::GetPlan(){

	double speed = maxInputNorm;
	node_t *node = &closestNode;
	node_t parent;
	std::list<node_t> path;
	printf("Node count:%d\n",nodeCount);
	printf("Closest dist: %f\n",closestDist);
	printf("goal: x,y:%f,%f\n",goalNode.pos.x,goalNode.pos.y);

	if(!goalreached){
		node = &goalNode;
		node->parent = &closestNode;
	}

	while(node != NULL){
		printf("x,y:%f,%f\n",node->pos.x,node->pos.y);
		path.push_front(*node);
		node = node->parent;
	}

	std::list<node_t>::iterator nodeIt;
	Plan newRoute;
	int count = 0;
	double ETA;
	for(nodeIt = path.begin(); nodeIt != path.end(); ++nodeIt){
		Position wp(proj.inverse(nodeIt->pos));
		if(count == 0){
			ETA = 0;
		}
		else{
			Position prevWP = newRoute.point(count-1).position();
			double distH    = wp.distanceH(prevWP);
			ETA             = ETA + distH/speed;
		}

		NavPoint np(wp,ETA);
		newRoute.addNavPoint(np);
		count++;
	}

	std::cout<<newRoute.toString()<<std::endl;

	return newRoute;
}

/*
int main(int argc,char* argv[]){

	srand(time(NULL));

	RRT_t RRT;

	// create bounding box

	Poly2D box;
	box.addVertex(0,0);
	box.addVertex(100,0);
	box.addVertex(100,100);
	box.addVertex(0,100);

	Poly3D bbox(box,0,100);
	//RRT.boundingBox = bbox;

	// obstacles
	Poly2D obs2D;
	//obs2D.addVertex(34.4838,-5.08);
	//obs2D.addVertex(32.455,-13.56);
	//obs2D.addVertex(41.9216,-11.867);
	//obs2D.addVertex(30,60);
	//Poly3D obs1(obs2D,-100,100);

	//Poly2D obs2D_2;
	//obs2D_2.addVertex(30,0);
	//obs2D_2.addVertex(60,0);
	//obs2D_2.addVertex(60,30);
	//obs2D_2.addVertex(30,30);
	//Poly3D obs2(obs2D_2,-100,100);

	//RRT.obstacleList.push_back(obs1);
	//RRT.obstacleList.push_back(obs2);

	int Nsteps = 1000;

	Vect3 pos(50,0,0);
	Vect3 vel(1,0,0);
	Vect3 trafficPos1(90,0,0);
	Vect3 trafficVel1(-1,0,0);

	std::vector<Vect3> TrafficPos;
	std::vector<Vect3> TrafficVel;

	TrafficPos.push_back(trafficPos1);
	TrafficVel.push_back(trafficVel1);

	Vect3 gpos(90,90,0);
	node_t goal;
	goal.pos = gpos;


	RRT.Initialize(pos,vel,TrafficPos,TrafficVel);

	RRT.xmin = 0;
	RRT.xmax = 100;
	RRT.ymin = 0;
	RRT.ymax = 100;
	RRT.zmin = -10;
	RRT.zmax = 100;

	RRT.dT = 1;

	RRT.CheckTrafficCollision(pos,vel,TrafficPos,TrafficVel);


	for(int i=0;i<Nsteps;i++){
		RRT.RRTStep();

		if(RRT.CheckGoal(goal)){
			printf("%f,%f\n",0,0);
			break;
		}
	}

	node_t node = RRT.nodeList.back();
	node_t parent;
	while(!node.parent.empty()){
		//printf("%f,%f\n",node.pos.x,node.pos.y);
		printf("%f,%f,%f,%f\n",node.pos.x,node.pos.y,
				node.trafficPos.front().x,node.trafficPos.front().y);
		//printf("parent: %f,%f\n",node.parent.front().pos.x,node.parent.front().pos.y);
		parent = node.parent.front();
		node = parent;
	}



}
*/
