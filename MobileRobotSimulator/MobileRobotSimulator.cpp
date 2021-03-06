#include "stdafx.h"
#include "GL\freeglut.h"
#include <iostream>

//Defines for trigonomatry
#define PI 3.1415926535898
#define Cos(th) cos(PI/180*(th))
#define Sin(th) sin(PI/180*(th))

//Defines for the transformation matrix used for inverse kinematics
#define Px transMatrix[3][0]
#define Py transMatrix[3][1]
#define Pz transMatrix[3][2]
#define r11 transMatrix[0][0]
#define r12 transMatrix[1][0]
#define r13 transMatrix[2][0]
#define r21 transMatrix[0][1]
#define r22 transMatrix[1][1]
#define r23 transMatrix[2][1]
#define r31 transMatrix[0][2]
#define r32 transMatrix[1][2]
#define r33 transMatrix[2][2]


//Defines for the DH parameters of the robot arm
#define a2 8
#define d3 0
#define a3 0
#define d4 8

#define square(param) pow(param,2)

/*  Globals */
double deltaT = 0.008; //Sampling time for animation
double dim = 40.0; // Dimension of orthogonal box 
const char *windowName = "Mobile Robot"; //Title of the OpenGL window

										 //Dimensions of the OpenGL window
int windowWidth = 1200;
int windowHeight = 1200;

//Enums Defining the various states necessary for computation
enum SystemStates { None, FKPlatform, IKPlatform, FKArm, IKArm, TRPlatform, TRArm, TRCombined };
enum TrajStates { Begin, Via1, Via2, Dest };
enum TrajStatesArm { BeginArm, Via1Arm, Via2Arm, DestArms };
enum CombinedTrajStates { Null, BaseMoving, ArmMoving };
CombinedTrajStates combinedState = Null;
SystemStates state = None;
TrajStates trajState = Begin;
TrajStatesArm trajStateArm = BeginArm;

//Angles for the camera

double cameraAngle1 = 0;
double cameraAngle2 = 0;
double cameraAngle3 = 0;
bool isReacheable = false;

int th = 340;   //azimuth of view angle 
int ph = 30;    // elevation of view angle 
int fov = 55;   // field of view for perspective 
int asp = 1;    // aspect ratio 

int MODULE_NUMBER = 0; // Storage for the module number from user input

double ARM_LENGTH = 16; //Combined a2 and d2 used for computation

						//Globals used for trajectory planning
double basePosX = 0;
double basePosY = 0;
double roboAngle = 0;

//Format of via points for the platform
struct ViaPoints {
	double x;
	double y;
	double z;
};

//Format of via points for the arm
struct ViaPointsArm {
	double x;
	double y;
	double z;
	double th4;
	double th5;
	double th6;
};

//Glboal via point that the robot should travel to in order to avoid collision
ViaPoints * avoidOBJ = new ViaPoints();

//Draws all of the frames required for modeling
//Blue: Z-axis
//Red: X-axis
//Green: Y-axis
void drawFrame() {

	glPushMatrix();
	glPushMatrix();
	glColor3f(1, 0, 0);
	glTranslatef(1, 0, 0);
	glScalef(2, 0.2, 0.2);
	glutSolidCube(1);
	glPopMatrix();
	glPushMatrix();
	glColor3f(0, 1, 0);
	glTranslatef(0, 1, 0);
	glScalef(0.2, 2, 0.2);
	glutSolidCube(1);
	glPopMatrix();
	glPushMatrix();
	glColor3f(0, 0, 1);
	glTranslatef(0, 0, 1);
	glScalef(0.2, 0.2, 2);
	glutSolidCube(1);
	glColor3f(1, 1, 1);
	glPopMatrix();
	glPopMatrix();

}

//Draws the grid of the work space
void DrawGrid(int HALF_GRID_SIZE)
{
	glBegin(GL_LINES);
	glColor3f(0.4, 0.4, 0.4);
	for (int i = -HALF_GRID_SIZE; i <= HALF_GRID_SIZE; i++)
	{
		if (i % 2 == 0) {
			glVertex3f((float)i, 0, (float)-HALF_GRID_SIZE);
			glVertex3f((float)i, 0, (float)HALF_GRID_SIZE);
			glVertex3f((float)-HALF_GRID_SIZE, 0, (float)i);
			glVertex3f((float)HALF_GRID_SIZE, 0, (float)i);
		}
	}
	glEnd();
}

//MobileRobot class containing all of the functions required to 
//compute the joint angles, positions of base and the arm, the orientation of the base
class MobileRobot {


public:

	const double WHEEL_CIRCUMFERENCE = 6.28; //Circumference of the wheel 2*PI*Diameter
	const double TRAVELED_PER_DEGREE = 0.01744; //Distance traveled by the robot per degree of rotation (Back Wheels)
	double wheelAngle1 = 0, wheelAngle2 = 0, wheelAngle3 = 0; //wheelAngle1,2: Back wheel angles; wheelAngle3: Front caster wheel angle
	double roboPx = 0, roboPy = 0; //Position of the robot in X,Y
	double movePx = 0, movePy = 0; //Variables used to compute trajectory
	double roboOriZ = 0; //Orientation of the base
	double theta1 = -45, theta2 = -45, theta3 = -45, theta4 = 45, theta5 = -45, theta6 = 0; //Joint angles for the arm
	double theta23 = 0;
	double transMatrix[4][4] = { 0 }; //T matrix for inv kin arm
	double time = 0; //Global time variable used for animation
	int numVia = 0; //Number of via points depending on the module

					//Variables used to store previous angle values used for inverse kin of arm
	double offth1 = 0;
	double offth2 = 0;
	double offth3 = 0;

	//Via points neccessary for all of the modules
	ViaPoints * viaPoint1 = new ViaPoints();
	ViaPoints * viaPoint2 = new ViaPoints();
	ViaPointsArm * viaPoint1Arm = new ViaPointsArm();
	ViaPointsArm * viaPoint2Arm = new ViaPointsArm();
	ViaPointsArm * viaPoint3Arm = new ViaPointsArm();
	ViaPointsArm * combinedInput = new ViaPointsArm();

	//Simple functions to convert angular units
	double convertRadsToDeg(double num) {
		return num * 180 / PI;
	}

	double convertDegToRad(double num) {
		return num * PI / 180;
	}

	//Draws position of the end effector as a solid sphere
	void drawEndEffectorPos(double x, double y, double z) {

		glPushMatrix();
		glTranslatef(x, y, z);
		glutSolidSphere(1, 20, 20);
		glPopMatrix();

	}

	//Inverse kinematics of the robot arm with custom parameters
	void calcJointWithParams(double px, double py, double pz, double th4, double th5, double th6) {

		double K = (square(px) + square(py) + square(pz) - square(a2) - square(d4) - (square(d3))) / (2 * a2);
		double R = sqrt(px * px + py * py);
		double r = sqrt(px * px + py * py);
		double Arm = -1, Elbow = -1;
		theta1 = convertRadsToDeg(atan2(-Arm * py * r, -Arm * px * r));
		R = sqrt(px * px + py * py + pz * pz);
		double sin_alpha = -pz / R;
		double cos_alpha = -Arm * r / R;
		double cos_beta = ((a2*a2 + R * R - (d4*d4)) / (2 * a2*R));
		double sin_beta = sqrt(1 - cos_beta * cos_beta);
		double sin_t2 = sin_alpha * cos_beta + Arm * Elbow * cos_alpha * sin_beta;
		double cos_t2 = cos_alpha * cos_beta - Arm * Elbow * sin_alpha * sin_beta;
		theta2 = convertRadsToDeg(atan2(sin_t2, cos_t2));
		double t2 = d4 * d4 + a3 * a3;
		double t = sqrt(t2);
		double cos_phi = (a2*a2 + t2 - R * R) / (2 * a2 * t);
		double sin_phi = Arm * Elbow * sqrt(1 - cos_phi * cos_phi);
		sin_beta = d4 / t;
		cos_beta = fabs(a3) / t;
		double sin_t3 = sin_phi * cos_beta - cos_phi * sin_beta;
		double cos_t3 = cos_phi * cos_beta + sin_phi * sin_beta;
		theta3 = convertRadsToDeg(atan2(sin_t3, cos_t3));
		theta3 = theta3 - 90;
		if (theta1 != theta1 || theta2 != theta2 || theta3 != theta3) {
			theta1 = 0;
			theta2 = 0;
			theta3 = 0;
		}
	}

	//Check if the positions entered by the user is reachable from robot's home position without setting the joint angles
	void checkIfReacheable(double px, double py, double pz, double th4, double th5, double th6) {
		double K = (square(px) + square(py) + square(pz) - square(a2) - square(d4) - (square(d3))) / (2 * a2);
		double R = sqrt(px * px + py * py);
		double r = sqrt(px * px + py * py);
		double Arm = -1, Elbow = -1;
		double thh1 = 0;
		double thh2 = 0;
		double thh3 = 0;
		thh1 = convertRadsToDeg(atan2(-Arm * py * r, -Arm * px * r));
		R = sqrt(px * px + py * py + pz * pz);
		double sin_alpha = -pz / R;
		double cos_alpha = -Arm * r / R;
		double cos_beta = ((a2*a2 + R * R - (d4*d4)) / (2 * a2*R));
		double sin_beta = sqrt(1 - cos_beta * cos_beta);
		double sin_t2 = sin_alpha * cos_beta + Arm * Elbow * cos_alpha * sin_beta;
		double cos_t2 = cos_alpha * cos_beta - Arm * Elbow * sin_alpha * sin_beta;
		thh2 = convertRadsToDeg(atan2(sin_t2, cos_t2));
		double t2 = d4 * d4 + a3 * a3;
		double t = sqrt(t2);
		double cos_phi = (a2*a2 + t2 - R * R) / (2 * a2 * t);
		double sin_phi = Arm * Elbow * sqrt(1 - cos_phi * cos_phi);
		sin_beta = d4 / t;
		cos_beta = fabs(a3) / t;
		double sin_t3 = sin_phi * cos_beta - cos_phi * sin_beta;
		double cos_t3 = cos_phi * cos_beta + sin_phi * sin_beta;
		thh3 = convertRadsToDeg(atan2(sin_t3, cos_t3));
		thh3 = theta3 - 90;
		if (thh1 != thh1 || thh2 != thh2 || thh3 != thh3) {

			isReacheable = false;
		}
		else {
			isReacheable = true;
		}


	}

	//Calculates inverse kinematics of the arm directly from user input
	void calculateJointAngles() {

		double K = (square(Px) + square(Py) + square(Pz) - square(a2) - square(d4) - (square(d3))) / (2 * a2);
		double R = sqrt(Px * Px + Py * Py);
		double r = sqrt(Px * Px + Py * Py);
		double Arm = -1, Elbow = -1;
		theta1 = convertRadsToDeg(atan2(-Arm * Py * r, -Arm * Px * r));
		R = sqrt(Px * Px + Py * Py + Pz * Pz);
		double sin_alpha = -Pz / R;
		double cos_alpha = -Arm * r / R;
		double cos_beta = ((a2*a2 + R * R - (d4*d4)) / (2 * a2*R));
		double sin_beta = sqrt(1 - cos_beta * cos_beta);
		double sin_t2 = sin_alpha * cos_beta + Arm * Elbow * cos_alpha * sin_beta;
		double cos_t2 = cos_alpha * cos_beta - Arm * Elbow * sin_alpha * sin_beta;
		theta2 = convertRadsToDeg(atan2(sin_t2, cos_t2));
		double t2 = d4 * d4 + a3 * a3;
		double t = sqrt(t2);
		double cos_phi = (a2*a2 + t2 - R * R) / (2 * a2 * t);
		double sin_phi = Arm * Elbow * sqrt(1 - cos_phi * cos_phi);
		sin_beta = d4 / t;
		cos_beta = fabs(a3) / t;
		double sin_t3 = sin_phi * cos_beta - cos_phi * sin_beta;
		double cos_t3 = cos_phi * cos_beta + sin_phi * sin_beta;
		theta3 = convertRadsToDeg(atan2(sin_t3, cos_t3));
		theta3 = theta3 - 90;
		if (theta1 != theta1 || theta2 != theta2 || theta3 != theta3) {
			theta1 = 0;
			theta2 = 0;
			theta3 = 0;
			std::cout << "NO SOLUTION!!!!";
		}
	}

	//Initializes transformation matrix for inverse kin
	void fillTransMatrix() {

		r11 = 1;
		r12 = 0;
		r13 = 0;
		r21 = 0;
		r22 = 1;
		r23 = 0;
		r31 = 0;
		r32 = 0;
		r33 = 1;
		transMatrix[3][3] = 1;

	}

	//Computes the forward kinematics of the base
	void ForwardKinematicBase() {

		double distance = wheelAngle1 / 360 * WHEEL_CIRCUMFERENCE;
		roboPx = -distance * Sin(wheelAngle3);
		roboPy = distance * Cos(wheelAngle3);

		glutPostRedisplay();

	}

	//Computes the inverse kinematics of the base
	void InverseKinematicBase() {
		double roboAngle;

		roboAngle = convertRadsToDeg(atan((double)((double)(movePy) / (double)(movePx))));
		double distance = sqrt((square(movePy) + square(movePx)));
		double revolutions = distance / WHEEL_CIRCUMFERENCE;
		double wheelRotationAngle = revolutions * 360;

		if (time <= 1) {
			wheelAngle3 = -roboAngle;
			wheelAngle1 = wheelRotationAngle;
			wheelAngle2 = wheelRotationAngle;
		}
		if (movePx < 0 && movePy < 0) {
			roboOriZ = -270 + roboAngle;
			roboPx = -abs(distance * Cos(roboAngle));
			roboPy = -abs(distance * Sin(roboAngle));
		}
		else if (movePx < 0 && movePy > 0) {
			roboOriZ = 90 + roboAngle;
			roboPx = -abs(distance * Cos(roboAngle));
			roboPy = abs(distance * Sin(roboAngle));
		}
		else if (movePx > 0 && movePy < 0) {
			roboOriZ = 270 + roboAngle;
			roboPx = abs(distance * Cos(roboAngle));
			roboPy = -abs(distance * Sin(roboAngle));
		}
		else {
			roboOriZ = -90 + roboAngle;
			roboPx = abs(distance * Cos(roboAngle));
			roboPy = abs(distance * Sin(roboAngle));
		}

		state = None;
	}


	//Trajectory planning of the arm only covering 0,1,2 via points
	void moveArmtoPos(ViaPoints * viaPoint1, ViaPoints * viaPoint2) {

		if (numVia == 0) {

			calcJointWithParams(viaPoint3Arm->x, viaPoint3Arm->y, viaPoint3Arm->z, viaPoint3Arm->th4, viaPoint3Arm->th5, viaPoint3Arm->th6);
			double angle1 = theta1;
			double angle2 = theta2;
			double angle3 = theta3;
			if (time < 1) {

				theta1 = angle1 * time;
				theta2 = angle2 * time;
				theta3 = angle3 * time;

			}
		}
		else if (numVia == 1) {
			if (trajStateArm == Via1Arm) {
				calcJointWithParams(viaPoint1Arm->x, viaPoint1Arm->y, viaPoint1Arm->z, viaPoint1Arm->th4, viaPoint1Arm->th5, viaPoint1Arm->th6);
				double angle1 = theta1;
				double angle2 = theta2;
				double angle3 = theta3;

				if (time < 1) {
					theta1 = angle1 * time;
					theta2 = angle2 * time;
					theta3 = angle3 * time;
				}

			}
			else if (trajStateArm == DestArms) {

				calcJointWithParams(viaPoint3Arm->x, viaPoint3Arm->y, viaPoint3Arm->z, viaPoint3Arm->th4, viaPoint3Arm->th5, viaPoint3Arm->th6);
				double angle1 = theta1;
				double angle2 = theta2;
				double angle3 = theta3;
				if (time < 1) {
					theta1 = (angle1 - offth1) * time + offth1;
					theta2 = (angle2 - offth2) * time + offth2;
					theta3 = (angle3 - offth3) * time + offth3;
				}
			}
		}
		else if (numVia = 2) {
			if (trajStateArm == Via2Arm) {
				calcJointWithParams(viaPoint1Arm->x, viaPoint1Arm->y, viaPoint1Arm->z, viaPoint1Arm->th4, viaPoint1Arm->th5, viaPoint1Arm->th6);
				double angle1 = theta1;
				double angle2 = theta2;
				double angle3 = theta3;

				if (time < 1) {
					theta1 = angle1 * time;
					theta2 = angle2 * time;
					theta3 = angle3 * time;
				}

			}
			else if (trajStateArm == Via1Arm) {

				calcJointWithParams(viaPoint2Arm->x, viaPoint2Arm->y, viaPoint2Arm->z, viaPoint2Arm->th4, viaPoint2Arm->th5, viaPoint2Arm->th6);
				double angle1 = theta1;
				double angle2 = theta2;
				double angle3 = theta3;
				if (time < 1) {
					theta1 = (angle1 - offth1) * time + offth1;
					theta2 = (angle2 - offth2) * time + offth2;
					theta3 = (angle3 - offth3) * time + offth3;
				}
			}
			else if (trajStateArm == DestArms) {

				calcJointWithParams(viaPoint3Arm->x, viaPoint3Arm->y, viaPoint3Arm->z, viaPoint3Arm->th4, viaPoint3Arm->th5, viaPoint3Arm->th6);
				double angle1 = theta1;
				double angle2 = theta2;
				double angle3 = theta3;
				if (time < 1) {
					theta1 = (angle1 - offth1) * time + offth1;
					theta2 = (angle2 - offth2) * time + offth2;
					theta3 = (angle3 - offth3) * time + offth3;
				}
			}


		}

		if ((time >= 1) && (trajStateArm == DestArms)) {
			state = None;
			time = 0;
		}

		else if ((time >= 1 && (trajStateArm == Via1Arm))) {
			offth1 = theta1;
			offth2 = theta2;
			offth3 = theta3;
			trajStateArm = DestArms;
			time = 0;
		}

		else if ((time >= 1 && (trajStateArm == Via2Arm))) {
			offth1 = theta1;
			offth2 = theta2;
			offth3 = theta3;
			trajStateArm = Via1Arm;
			time = 0;


		}

		time = time + deltaT;

	}
	double roboAngleoff = 0;
	double roboPxOff = 0;
	double roboPyOff = 0;
	double roboOriOff = 0;
	//Trajectory planning of the base covering 0, 1, 2 via points
	void moveBasetoPos(double px, double py, int numVia, ViaPoints * viaPoint1, ViaPoints * viaPoint2) {

		double roboAngle;
		if (numVia == 0) {

			roboAngle = convertRadsToDeg(atan((py) / (px)));
			double distance = sqrt((square(py) + square(px)));
			double revolutions = distance / WHEEL_CIRCUMFERENCE;
			double wheelRotationAngle = revolutions * 360;
			if (time <= 1) {
				wheelAngle3 = roboAngle * time;
				wheelAngle1 = wheelRotationAngle * time;
				wheelAngle2 = wheelRotationAngle * time;
			}
			if (px < 0 && py < 0) {
				roboOriZ = -270 + roboAngle * time;
				roboPx = -abs(distance * Cos(roboAngle)*time);
				roboPy = -abs(distance * Sin(roboAngle)*time);
			}
			else if (px < 0 && py> 0) {
				roboOriZ = 90 + roboAngle * time;
				roboPx = -abs(distance * Cos(roboAngle)*time);
				roboPy = abs(distance * Sin(roboAngle)*time);
			}
			else if (px > 0 && py < 0) {
				roboOriZ = 270 + roboAngle * time;
				roboPx = abs(distance * Cos(roboAngle)*time);
				roboPy = -abs(distance * Sin(roboAngle)*time);
			}
			else {
				roboOriZ = -90 + roboAngle * time;
				roboPx = abs(distance * Cos(roboAngle)*time);
				roboPy = abs(distance * Sin(roboAngle)*time);
			}

		}
		else if (numVia == 1) {
			if (trajState == Via1) {
				roboAngle = convertRadsToDeg(atan((double)((double)viaPoint1->y) / ((double)viaPoint1->x)));
				double distance = sqrt((square(viaPoint1->y) + square(viaPoint1->x)));
				double revolutions = distance / WHEEL_CIRCUMFERENCE;
				double wheelRotationAngle = revolutions * 360;
				if (time <= 1) {
					wheelAngle3 = roboAngle * time;
					wheelAngle1 = wheelRotationAngle * time;
					wheelAngle2 = wheelRotationAngle * time;
				}
				if (viaPoint1->x < 0 && viaPoint1->y < 0) {
					roboOriZ = -270 + roboAngle * time;
					roboPx = -abs(distance * Cos(roboAngle)*time);
					roboPy = -abs(distance * Sin(roboAngle)*time);
				}
				else if (viaPoint1->x < 0 && viaPoint1->y> 0) {
					roboOriZ = 90 + roboAngle * time;
					roboPx = -abs(distance * Cos(roboAngle)*time);
					roboPy = abs(distance * Sin(roboAngle)*time);
				}
				else if (viaPoint1->x > 0 && viaPoint1->y < 0) {
					roboOriZ = 270 + roboAngle * time;
					roboPx = abs(distance * Cos(roboAngle)*time);
					roboPy = -abs(distance * Sin(roboAngle)*time);
				}
				else {
					roboOriZ = -90 + roboAngle * time;
					roboPx = abs(distance * Cos(roboAngle)*time);
					roboPy = abs(distance * Sin(roboAngle)*time);
				}
			}

			else if (trajState == Dest) {
				roboAngle = convertRadsToDeg((double)atan(((double)py - (double)viaPoint1->y) / ((double)px - (double)viaPoint1->x)));
				double distance = (double)sqrt((square((double)py - (double)viaPoint1->y) + square((double)px - (double)viaPoint1->x)));
				double revolutions = distance / WHEEL_CIRCUMFERENCE;
				double wheelRotationAngle = revolutions * 360;

				if (time <= 1) {
					wheelAngle3 = roboAngle * time;
					wheelAngle1 = wheelRotationAngle * time;
					wheelAngle2 = wheelRotationAngle * time;
				}
				if (px - viaPoint1->x < 0 && py - viaPoint1->y < 0) {
					roboOriZ = (-270 + roboAngle - roboOriOff) * time + roboOriOff;
					roboPx = viaPoint1->x - abs(distance * Cos(roboAngle)*time);
					roboPy = viaPoint1->y - abs(distance * Sin(roboAngle)*time);
				}
				else if (px - viaPoint1->x < 0 && py - viaPoint1->y> 0) {
					roboOriZ = (90 + roboAngle - roboOriOff) * time + roboOriOff;
					roboPx = viaPoint1->x + -abs(distance * Cos(roboAngle)*time);
					roboPy = viaPoint1->y + abs(distance * Sin(roboAngle)*time);
				}
				else if (px - viaPoint1->x > 0 && py - viaPoint1->y < 0) {
					roboOriZ = (270 + roboAngle - roboOriOff) * time + roboOriOff;
					roboPx = viaPoint1->x + abs(distance * Cos(roboAngle)*time);
					roboPy = viaPoint1->y - abs(distance * Sin(roboAngle)*time);
				}
				else {
					roboOriZ = (-90 + roboAngle - roboOriOff) * time + roboOriOff;
					roboPx = viaPoint1->x + abs(distance * Cos(roboAngle)*time);
					roboPy = viaPoint1->y + abs(distance * Sin(roboAngle)*time);
				}
			}
		}
		else if (numVia == 2) {
			if (trajState == Via2) {
				roboAngle = convertRadsToDeg(atan((viaPoint1->y) / (viaPoint1->x)));
				double distance = sqrt((square(viaPoint1->y) + square(viaPoint1->x)));
				double revolutions = distance / WHEEL_CIRCUMFERENCE;
				double wheelRotationAngle = revolutions * 360;
				if (time <= 1) {
					wheelAngle3 = roboAngle * time;
					wheelAngle1 = wheelRotationAngle * time;
					wheelAngle2 = wheelRotationAngle * time;
				}
				if (viaPoint1->x < 0 && viaPoint1->y < 0) {
					roboOriZ = -270 + roboAngle * time;
					roboPx = -abs(distance * Cos(roboAngle)*time);
					roboPy = -abs(distance * Sin(roboAngle)*time);
				}
				else if (viaPoint1->x < 0 && viaPoint1->y> 0) {
					roboOriZ = 90 + roboAngle * time;
					roboPx = -abs(distance * Cos(roboAngle)*time);
					roboPy = abs(distance * Sin(roboAngle)*time);
				}
				else if (viaPoint1->x > 0 && viaPoint1->y < 0) {
					roboOriZ = 270 + roboAngle * time;
					roboPx = abs(distance * Cos(roboAngle)*time);
					roboPy = -abs(distance * Sin(roboAngle)*time);
				}
				else {
					roboOriZ = -90 + roboAngle * time;
					roboPx = abs(distance * Cos(roboAngle)*time);
					roboPy = abs(distance * Sin(roboAngle)*time);
				}
			}
			else if (trajState == Via1) {
				roboAngle = convertRadsToDeg(atan((viaPoint2->y - viaPoint1->y) / (viaPoint2->x - viaPoint1->x)));
				double distance = sqrt((square(viaPoint2->y - viaPoint1->y) + square(viaPoint2->x - viaPoint1->x)));
				double revolutions = distance / WHEEL_CIRCUMFERENCE;
				double wheelRotationAngle = revolutions * 360;
				if (time <= 1) {
					wheelAngle3 = roboAngle * time;
					wheelAngle1 = wheelRotationAngle * time;
					wheelAngle2 = wheelRotationAngle * time;
				}
				if (viaPoint2->x - viaPoint1->x < 0 && viaPoint2->y - viaPoint1->y < 0) {
					roboOriZ = (-270 + roboAngle - roboOriOff) * time + roboOriOff;
					roboPx = viaPoint1->x - abs(distance * Cos(roboAngle)*time);
					roboPy = viaPoint1->y - abs(distance * Sin(roboAngle)*time);
				}
				else if (viaPoint2->x - viaPoint1->x  < 0 && viaPoint2->y - viaPoint1->y > 0) {
					roboOriZ = (90 + roboAngle - roboOriOff) * time + roboOriOff;
					roboPx = viaPoint1->x - abs(distance * Cos(roboAngle)*time);
					roboPy = viaPoint1->y + abs(distance * Sin(roboAngle)*time);
				}
				else if (viaPoint2->x - viaPoint1->x  > 0 && viaPoint2->y - viaPoint2->y < 0) {
					roboOriZ = (270 + roboAngle - roboOriOff) * time + roboOriOff;
					roboPx = viaPoint1->x + abs(distance * Cos(roboAngle)*time);
					roboPy = viaPoint1->y - abs(distance * Sin(roboAngle)*time);
				}
				else {
					roboOriZ = (-90 + roboAngle - roboOriOff) * time + roboOriOff;
					roboPx = viaPoint1->x + abs(distance * Cos(roboAngle)*time);
					roboPy = viaPoint1->y + abs(distance * Sin(roboAngle)*time);
				}
			}
			else if (trajState == Dest) {
				roboAngle = convertRadsToDeg(atan((py - viaPoint2->y) / (px - viaPoint2->x)));
				double distance = sqrt((square(py - viaPoint2->y) + square(px - viaPoint2->x)));
				double revolutions = distance / WHEEL_CIRCUMFERENCE;
				double wheelRotationAngle = revolutions * 360;
				if (time <= 1) {
					wheelAngle3 = roboAngle * time;
					wheelAngle1 = wheelRotationAngle * time;
					wheelAngle2 = wheelRotationAngle * time;
				}
				if (px - viaPoint2->x < 0 && py - viaPoint2->y < 0) {
					roboOriZ = (-270 + roboAngle - roboOriOff) * time + roboOriOff;
					roboPx = viaPoint2->x - abs(distance * Cos(roboAngle)*time);
					roboPy = viaPoint2->y - abs(distance * Sin(roboAngle)*time);
				}
				else if (px - viaPoint2->x < 0 && py - viaPoint2->y> 0) {
					roboOriZ = (90 + roboAngle - roboOriOff) * time + roboOriOff;
					roboPx = viaPoint2->x + -abs(distance * Cos(roboAngle)*time);
					roboPy = viaPoint2->y + abs(distance * Sin(roboAngle)*time);
				}
				else if (px - viaPoint2->x > 0 && py - viaPoint2->y < 0) {
					roboOriZ = (270 + roboAngle - roboOriOff) * time + roboOriOff;
					roboPx = viaPoint2->x + abs(distance * Cos(roboAngle)*time);
					roboPy = viaPoint2->y - abs(distance * Sin(roboAngle)*time);
				}
				else {
					roboOriZ = (-90 + roboAngle - roboOriOff) * time + roboOriOff;
					roboPx = viaPoint2->x + abs(distance * Cos(roboAngle)*time);
					roboPy = viaPoint2->y + abs(distance * Sin(roboAngle)*time);
				}
			}

		}
		time = time + deltaT;
		if ((time >= 1) && (trajState == Dest)) {
			if (combinedState == Null) {
				state = None;
			}
			time = 0;
			combinedState = ArmMoving;
			roboAngle = 0;
		}
		else if ((time >= 1 && (trajState == Via1)))
		{
			roboAngleoff = roboAngle;
			roboOriOff = roboOriZ;
			trajState = Dest;
			time = 0;
		}
		else if ((time >= 1 && (trajState == Via2))) {

			roboAngleoff = roboAngle;
			roboOriOff = roboOriZ;
			trajState = Via1;
			time = 0;
		}

	}

	//Trajectory planning arm and base combined
	void computeCombinedTrajectory() {

		//if arm is within reach when robot is docked at origin
		if (isReacheable) {
			combinedState = ArmMoving;
			calcJointWithParams(combinedInput->x, combinedInput->y, combinedInput->z, combinedInput->th4, combinedInput->th5, combinedInput->th6);
			double angle1 = theta1;
			double angle2 = theta2;
			double angle3 = theta3;

			if (time <= 1) {

				theta1 = angle1 * time;
				theta2 = (angle2 + 90) * time - 90;
				theta3 = angle3 * time;

			}
		}
		else {
			if (combinedState == ArmMoving) {
				calcJointWithParams(combinedInput->x - basePosX, combinedInput->y - basePosY, combinedInput->z, combinedInput->th4, combinedInput->th5, combinedInput->th6);
				double angle1 = theta1;
				double angle2 = theta2;
				double angle3 = theta3;
				if (time <= 1) {

					theta1 = angle1 * time;
					theta2 = (angle2 + 90) * time - 90;
					theta3 = angle3 * time;

				}

			}

		}

		if (time >= 1) {
			time = 0;
			combinedState = Null;
			state = None;
		}
		time = time + deltaT;

	}

	//This function is responsible for drawing all components of the robot
	void initMobileRobot() {
		//draw the mobile platform
		glPushMatrix();
		glRotatef(-90, 1, 0, 0);

		if (state == TRCombined) {
			glPushMatrix();
			glTranslatef(-25 + 15, -25 + 15, 5);
			glPushMatrix();
			glScalef(2, 2, 10);
			glColor3f(1, 0, 0);
			glutSolidCube(1); //Obstacle 1
			glPopMatrix();
			glPopMatrix();
			glPushMatrix();
			glTranslatef(-25 + 45, -25 + 20, 5);
			glPushMatrix();
			glScalef(2, 2, 10);
			glColor3f(0, 1, 0);
			glutSolidCube(1); //Obstacle 2
			glPopMatrix();
			glPopMatrix();
			glPushMatrix();
			glTranslatef(-25 + 20, -25 + 45, 5);
			glPushMatrix();
			glScalef(2, 2, 10);
			glColor3f(0, 0, 1);
			glutSolidCube(1); //Obstacle 3
			glPopMatrix();
			glPopMatrix();
			glPushMatrix();
			drawEndEffectorPos(-25 + avoidOBJ->x, -25 + avoidOBJ->y, 0); // draw via point
			glPopMatrix();
		}

		glPushMatrix();
		glTranslatef(-25 + roboPx, -25 + roboPy, 2);
		glRotatef(roboOriZ, 0, 0, 1);
		glPushMatrix();
		glTranslatef(0, 0, 1);
		drawFrame();
		glPopMatrix();
		glPushMatrix();
		glScalef(4, 8, 1);
		glutSolidCube(1);
		glPopMatrix();

		//Left Wheel
		glPushMatrix();
		glRotatef(-90, 0, 1, 0);		 //rotate frame so that the z-axis is the axis of rotation
		glPushMatrix();
		glTranslatef(0, -4, 2);
		glRotatef(wheelAngle1, 0, 0, 1); //rotation of wheel 1
		drawFrame();
		glColor3f(0.8, 0.8, 0.8);
		glScalef(2, 2, 1);
		glutSolidCylinder(1, 1, 20, 20);
		glPopMatrix();
		glPopMatrix();

		//Right Wheel
		glPushMatrix();
		glRotatef(-90, 0, 1, 0);		 //rotate frame so that the z-axis is the axis of rotation
		glPushMatrix();
		glTranslatef(0, -4, -2);
		glRotatef(wheelAngle2, 0, 0, 1); //rotation of wheel 2
		glPushMatrix();
		glTranslatef(0, 0, -1);
		drawFrame();
		glPopMatrix();
		glColor3f(0.8, 0.8, 0.8);
		glScalef(2, 2, -1);
		glutSolidCylinder(1, 1, 20, 20);
		glPopMatrix();
		glPopMatrix();

		//Front Wheel (Caster Wheel)
		glPushMatrix();
		glPushMatrix();
		glTranslatef(0, 4, 0);
		glRotatef(wheelAngle3, 0, 0, 1); //rotation of wheel 3
		drawFrame();
		glColor3f(0.8, 0.8, 0.8);
		glScalef(1, 1, 2);
		glRotatef(-90, 0, 1, 0);
		glutSolidCylinder(1, 1, 20, 20);
		glPopMatrix();
		glPopMatrix();


		//Arm
		//joint 1
		glPushMatrix();
		glRotatef(90, 0, 0, 1);
		glRotatef(theta1, 0, 0, 1);
		glPushMatrix();
		glTranslatef(0, 0, 0.5);
		drawFrame();
		glPopMatrix();
		glPushMatrix();
		glTranslatef(1.2, 0, 1.5);
		glColor3f(1, 1, 0);
		glutSolidSphere(0.5, 20, 20);
		glPopMatrix();
		glColor3f(0.6, 0.6, 0.6);
		glTranslatef(0, 0, 1);
		glutWireSphere(1.5, 20, 20);

		//joint 2
		glRotatef(-90, 1, 0, 0);
		glPushMatrix();
		glRotatef(theta2, 0, 0, 1);

		drawFrame();
		glColor3f(0.6, 0.6, 0.6);
		glTranslatef(a2 / 2, 0, 0);
		glPushMatrix();
		glScalef(a2, 0.5, 0.5);
		glutWireCube(1);
		glPopMatrix();

		//joint 3
		glPushMatrix();
		glRotatef(-90, 0, 0, 1);
		glTranslatef(0, d4 / 2, 0);
		glRotatef(theta3, 0, 0, 1);
		drawFrame();
		glColor3f(0.6, 0.6, 0.6);
		glTranslatef(0, d4 / 2, 0);
		glPushMatrix();
		glScalef(0.5, d4, 0.5);
		glutWireCube(1);
		glPopMatrix();

		//Right Brace for Joint 3
		glPushMatrix();
		glTranslatef(0, -d4 / 2, 0.5);
		glScalef(0.25, 1, 0.25);
		glutWireCube(1);
		glPopMatrix();

		//Left Brace for Joint 3
		glPushMatrix();
		glTranslatef(0, -d4 / 2, -0.5);
		glScalef(0.25, 1, 0.25);
		glutWireCube(1);
		glPopMatrix();


		//joint 4
		glRotatef(-90, 1, 0, 0);
		glPushMatrix();
		glTranslatef(0, 0, 2);
		glRotatef(theta4, 0, 0, 1);
		glPushMatrix();
		glTranslatef(0, 0, 1);
		drawFrame();
		glPopMatrix();
		glColor3f(0.6, 0.6, 0.6);
		glPushMatrix();
		glutWireCylinder(1, 2, 20, 20);
		glPopMatrix();

		//joint 5
		glRotatef(90, 1, 0, 0);
		glPushMatrix();
		glTranslatef(0, 1, 0);
		glRotatef(theta5, 0, 0, 1);
		drawFrame();
		glColor3f(0.6, 0.6, 0.6);
		glTranslatef(0, 0.5, 0);
		glPushMatrix();
		glScalef(0.5, 1, 0.5);
		glutWireCube(1);
		glPopMatrix();

		//joint 6
		glRotatef(-90, 1, 0, 0);
		glPushMatrix();
		glTranslatef(0, 0, 0.5);
		glRotatef(theta6, 0, 0, 1);
		drawFrame();
		glColor3f(0.6, 0.6, 0.6);
		glutSolidCylinder(1, 0.5, 20, 20);
		glTranslatef(0, 0, 0.5);
		glPushMatrix();
		glScalef(3, 5, 0.5);
		glutWireCube(1);

		glPopMatrix();
		glPopMatrix();
		glPopMatrix();
		glPopMatrix();
		glPopMatrix();
		glPopMatrix();
		glPopMatrix();
		glPopMatrix();
		glPopMatrix();
	}

private:

};

//Instantiate the mobile robot class
MobileRobot * mobileRobot = new MobileRobot();

//Draws the frame for the camera (Home Position)
void drawCameraFrame() {
	glPushMatrix();
	glRotatef(-90, 1, 0, 0);
	glTranslatef(-25, -25, 2);
	drawFrame();
	glPopMatrix();
}

//Orthogonal Projection
void project()
{
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(-dim * asp, +dim * asp, -dim, +dim, -dim, +dim);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
}


//Sets the angle of the camera
void setEye()
{

	glRotatef(ph + cameraAngle1, 1, 0, 0);
	glRotatef(th + cameraAngle2, 0, 1, 0);
	glRotatef(cameraAngle3, 0, 0, 1);

}

//Handles all of the modules requested by the users, set's the state to None either inside here
//or in the functions themselves to trigger the console after the event finishes
void HandleEvent() {

	switch (state) {
	case FKPlatform:
		mobileRobot->ForwardKinematicBase();
		glutPostRedisplay();
		state = None;
		break;
	case IKPlatform:
		mobileRobot->InverseKinematicBase();
		glutPostRedisplay();
		state = None;
		break;
	case IKArm:
		mobileRobot->calculateJointAngles();
		glutPostRedisplay();
		state = None;
		break;
	case FKArm:
		glutPostRedisplay();
		state = None;
		break;
	case TRPlatform:
		mobileRobot->moveBasetoPos(mobileRobot->movePx, mobileRobot->movePy, mobileRobot->numVia, mobileRobot->viaPoint1, mobileRobot->viaPoint2);
		glutPostRedisplay();
		break;
	case TRArm:
		mobileRobot->moveArmtoPos(mobileRobot->viaPoint1, mobileRobot->viaPoint2);
		glutPostRedisplay();
		break;
	case TRCombined:

		mobileRobot->computeCombinedTrajectory();
		if (combinedState != ArmMoving) {
			mobileRobot->moveBasetoPos(basePosX, basePosY, mobileRobot->numVia, avoidOBJ, mobileRobot->viaPoint1);
		}
		glutPostRedisplay();
		break;

	}

}

//Function mainly used for the UI.
//Has some minor computations regarding the trajectory planning of the base and the arm (undesirable)
//Sets the state to the module that the user has selected and calls HandleEvent();
void ioFunc() {

	std::cout << std::endl;
	std::cout << "1: Forward Kinematics of the Mobile Platform" << std::endl;
	std::cout << "2: Inverse Kinematics of the Mobile Platform" << std::endl;
	std::cout << "3: Forward Kinematics of the Arm" << std::endl;
	std::cout << "4: Inverse Kinematics of the Arm" << std::endl;
	std::cout << "5: Move Mobile Robot to Position" << std::endl;
	std::cout << "6: Move Arm to Position" << std::endl;
	std::cout << "7: Combined Trajectory of Arm and Base" << std::endl;
	std::cout << std::endl;
	std::cout << "Enter the mode number: ";
	std::cin >> MODULE_NUMBER;
	std::cout << std::endl;
	switch (MODULE_NUMBER) {
	case 1:
		state = FKPlatform;
		std::cout << "Enter the Front Wheel Angle of the Mobile Platform: ";
		std::cin >> mobileRobot->wheelAngle3;
		mobileRobot->roboOriZ = mobileRobot->wheelAngle3;


		std::cout << "Enter the Back Wheel Angle of the Mobile Platform: ";
		std::cin >> mobileRobot->wheelAngle1;
		mobileRobot->wheelAngle2 = mobileRobot->wheelAngle1;
		std::cout << std::endl;
		glutPostRedisplay();
		HandleEvent();
		break;
	case 2:
		state = IKPlatform;
		std::cout << "Enter the robot's destination in x:";
		std::cin >> mobileRobot->movePx;
		std::cout << "Enter the robot's destination in y:";
		std::cin >> mobileRobot->movePy;
		HandleEvent();
		break;
	case 3:
		state = FKArm;
		std::cout << "Enter Joint Angle 1: ";
		while ((std::cin >> mobileRobot->theta1).fail()) {
			std::cout << std::endl;
			std::cin.clear();
			std::cin.ignore();
			std::cout << "Please enter a numerical value!!!" << std::endl;
		}
		std::cout << "Enter Joint Angle 2: ";
		while ((std::cin >> mobileRobot->theta2).fail()) {
			std::cout << std::endl;
			std::cin.clear();
			std::cin.ignore();
			std::cout << "Please enter a numerical value!!!" << std::endl;
		}
		std::cout << "Enter Joint Angle 3: ";
		while ((std::cin >> mobileRobot->theta3).fail()) {
			std::cout << std::endl;
			std::cin.clear();
			std::cin.ignore();
			std::cout << "Please enter a numerical value!!!" << std::endl;
		}
		std::cout << "Enter Joint Angle 4: ";
		while ((std::cin >> mobileRobot->theta4).fail()) {
			std::cout << std::endl;
			std::cin.clear();
			std::cin.ignore();
			std::cout << "Please enter a numerical value!!!" << std::endl;
		}
		std::cout << "Enter Joint Angle 5: ";
		while ((std::cin >> mobileRobot->theta5).fail()) {
			std::cout << std::endl;
			std::cin.clear();
			std::cin.ignore();
			std::cout << "Please enter a numerical value!!!" << std::endl;
		}
		std::cout << "Enter Joint Angle 6: ";
		while ((std::cin >> mobileRobot->theta6).fail()) {
			std::cout << std::endl;
			std::cin.clear();
			std::cin.ignore();
			std::cout << "Please enter a numerical value!!!" << std::endl;
		}
		glutPostRedisplay();
		HandleEvent();
		break;
	case 4:
		state = IKArm;
		mobileRobot->fillTransMatrix();
		std::cout << "Enter Px: ";
		while ((std::cin >> mobileRobot->Px).fail()) {
			std::cout << std::endl;
			std::cin.clear();
			std::cin.ignore();
			std::cout << "Please enter a numerical value!!!" << std::endl;
		}
		std::cout << "Enter Py: ";
		while ((std::cin >> mobileRobot->Py).fail()) {
			std::cout << std::endl;
			std::cin.clear();
			std::cin.ignore();
			std::cout << "Please enter a numerical value!!!" << std::endl;
		}
		std::cout << "Enter Pz: ";
		while ((std::cin >> mobileRobot->Pz).fail()) {
			std::cout << std::endl;
			std::cin.clear();
			std::cin.ignore();
			std::cout << "Please enter a numerical value!!!" << std::endl;
		}
		std::cout << "Enter Row Angle: ";
		while ((std::cin >> mobileRobot->theta4).fail()) {
			std::cout << std::endl;
			std::cin.clear();
			std::cin.ignore();
			std::cout << "Please enter a numerical value!!!" << std::endl;
		}
		std::cout << "Enter Pitch Angle: ";
		while ((std::cin >> mobileRobot->theta5).fail()) {
			std::cout << std::endl;
			std::cin.clear();
			std::cin.ignore();
			std::cout << "Please enter a numerical value!!!" << std::endl;
		}
		std::cout << "Enter Yaw Angle: ";
		while ((std::cin >> mobileRobot->theta6).fail()) {
			std::cout << std::endl;
			std::cin.clear();
			std::cin.ignore();
			std::cout << "Please enter a numerical value!!!" << std::endl;
		}

		HandleEvent();
		break;
	case 5:
		state = TRPlatform;
		std::cout << "Enter the number of via points (0, 1, 2):";
		std::cin >> mobileRobot->numVia;
		if (mobileRobot->numVia == 1) {
			trajState = Via1;
			std::cout << "Enter the via point 1 in x:";
			std::cin >> mobileRobot->viaPoint1->x;
			std::cout << "Enter the via point 1 in y:";
			std::cin >> mobileRobot->viaPoint1->y;
		}
		else if (mobileRobot->numVia == 2) {
			trajState = Via2;
			std::cout << "Enter the via point 1 in x:";
			std::cin >> mobileRobot->viaPoint1->x;
			std::cout << "Enter the via point 1 in y:";
			std::cin >> mobileRobot->viaPoint1->y;
			std::cout << "Enter the via point 2 in x:";
			std::cin >> mobileRobot->viaPoint2->x;
			std::cout << "Enter the via point 2 in y:";
			std::cin >> mobileRobot->viaPoint2->y;
		}

		else {
			trajState = Dest;
		}
		std::cout << "Enter the robot's destination in x:";
		std::cin >> mobileRobot->movePx;
		std::cout << "Enter the robot's destination in y:";
		std::cin >> mobileRobot->movePy;
		HandleEvent();
		break;

	case 6:
		state = TRArm;
		std::cout << "Enter the number of via points (0, 1, 2):";
		std::cin >> mobileRobot->numVia;
		if (mobileRobot->numVia == 1) {
			trajStateArm = Via1Arm;
			std::cout << std::endl;
			std::cout << "Enter the x-value of the viapoint:";
			std::cin >> mobileRobot->viaPoint1Arm->x;
			std::cout << "Enter the y-value of the viapoint:";
			std::cin >> mobileRobot->viaPoint1Arm->y;
			std::cout << "Enter the z-value of the viapoint:";
			std::cin >> mobileRobot->viaPoint1Arm->z;
			std::cout << "Enter the angle of the wrist joint (row): ";
			std::cin >> mobileRobot->viaPoint1Arm->th4;
			std::cout << "Enter the angle of the wrist joint (pitch): ";
			std::cin >> mobileRobot->viaPoint1Arm->th5;
			std::cout << "Enter the angle of the wrist joint (yaw): ";
			std::cin >> mobileRobot->viaPoint1Arm->th6;

		}
		else if (mobileRobot->numVia == 2) {
			trajStateArm = Via2Arm;
			std::cout << std::endl;
			std::cout << "Enter the x-value of the 1st viapoint:";
			std::cin >> mobileRobot->viaPoint1Arm->x;
			std::cout << "Enter the y-value of the 1st viapoint:";
			std::cin >> mobileRobot->viaPoint1Arm->y;
			std::cout << "Enter the z-value of the 1st viapoint:";
			std::cin >> mobileRobot->viaPoint1Arm->z;
			std::cout << "Enter the angle of the wrist joint (row): ";
			std::cin >> mobileRobot->viaPoint1Arm->th4;
			std::cout << "Enter the angle of the wrist joint (pitch): ";
			std::cin >> mobileRobot->viaPoint1Arm->th5;
			std::cout << "Enter the angle of the wrist joint (yaw): ";
			std::cin >> mobileRobot->viaPoint1Arm->th6;
			std::cout << std::endl;
			std::cout << "Enter the x-value of the 2nd viapoint:";
			std::cin >> mobileRobot->viaPoint2Arm->x;
			std::cout << "Enter the y-value of the 2nd viapoint:";
			std::cin >> mobileRobot->viaPoint2Arm->y;
			std::cout << "Enter the z-value of the 2nd viapoint:";
			std::cin >> mobileRobot->viaPoint2Arm->z;
			std::cout << "Enter the angle of the wrist joint (row): ";
			std::cin >> mobileRobot->viaPoint2Arm->th4;
			std::cout << "Enter the angle of the wrist joint (pitch): ";
			std::cin >> mobileRobot->viaPoint2Arm->th5;
			std::cout << "Enter the angle of the wrist joint (yaw): ";
			std::cin >> mobileRobot->viaPoint2Arm->th6;
		}
		else {
			trajStateArm = DestArms;
		}
		std::cout << std::endl;
		std::cout << "Enter the x-value of the arm's destination:";
		std::cin >> mobileRobot->viaPoint3Arm->x;
		std::cout << "Enter the y-value of the arm's destination:";
		std::cin >> mobileRobot->viaPoint3Arm->y;
		std::cout << "Enter the z-value of the arm's destination:";
		std::cin >> mobileRobot->viaPoint3Arm->z;
		std::cout << "Enter the angle of the wrist joint (row): ";
		std::cin >> mobileRobot->viaPoint3Arm->th4;
		std::cout << "Enter the angle of the wrist joint (pitch): ";
		std::cin >> mobileRobot->viaPoint3Arm->th5;
		std::cout << "Enter the angle of the wrist joint (yaw): ";
		std::cin >> mobileRobot->viaPoint3Arm->th6;

		HandleEvent();
		break;
	case 7:
		state = TRCombined;
		std::cout << "Enter the x-value of the end-effector destination:";
		std::cin >> mobileRobot->combinedInput->x;
		std::cout << "Enter the y-value of the end-effector destination:";
		std::cin >> mobileRobot->combinedInput->y;
		std::cout << "Enter the z-value of the end-effector destination:";
		std::cin >> mobileRobot->combinedInput->z;
		std::cout << "Enter the angle of the wrist joint (row): ";
		std::cin >> mobileRobot->combinedInput->th4;
		std::cout << "Enter the angle of the wrist joint (pitch): ";
		std::cin >> mobileRobot->combinedInput->th5;
		std::cout << "Enter the angle of the wrist joint (yaw): ";
		std::cin >> mobileRobot->combinedInput->th6;

		mobileRobot->theta1 = 0;
		mobileRobot->theta2 = -90;
		mobileRobot->theta3 = 0;


		mobileRobot->checkIfReacheable(mobileRobot->combinedInput->x, mobileRobot->combinedInput->y,
			mobileRobot->combinedInput->z, mobileRobot->combinedInput->th4,
			mobileRobot->combinedInput->th5, mobileRobot->combinedInput->th6);
		double AL1OBJ1 = mobileRobot->convertRadsToDeg(atan((double)17 / 13));
		double AL2OBJ1 = mobileRobot->convertRadsToDeg(atan((double)13 / 17));
		double AL1OBJ2 = mobileRobot->convertRadsToDeg(atan((double)18 / 47));
		double AL2OBJ2 = mobileRobot->convertRadsToDeg(atan((double)22 / 43));
		double AL1OBJ3 = mobileRobot->convertRadsToDeg(atan((double)47 / 18));
		double AL2OBJ3 = mobileRobot->convertRadsToDeg(atan((double)43 / 22));
		double DLOBJ1 = sqrt(square(13) + square(13));
		double DLOBJ2 = sqrt(square(18) + square(47));
		double DLOBJ3 = sqrt(square(18) + square(47));


		//if arm is out of reach of origin, will need to check if an obstacle is in the way before moving
		if (!isReacheable) {

			combinedState = BaseMoving;
			basePosX = mobileRobot->combinedInput->x + 5;
			basePosY = mobileRobot->combinedInput->y + 5;
			double distance = sqrt((double)square(basePosX) + square((double)square(basePosY)));
			roboAngle = mobileRobot->convertRadsToDeg(atan((double)basePosY / (double)basePosX));

			if (roboAngle <= AL1OBJ1 && roboAngle >= AL2OBJ1 && distance >= DLOBJ1) {
				avoidOBJ->x = 10;
				avoidOBJ->y = 20;
				trajState = Via1;
				mobileRobot->numVia = 1;
			}
			else if (roboAngle >= 19 && roboAngle <= AL2OBJ2 && distance >= DLOBJ2) {
				avoidOBJ->x = 50;
				avoidOBJ->y = 10;
				trajState = Via1;
				mobileRobot->numVia = 1;
			}
			else if (roboAngle <= AL1OBJ3 && roboAngle >= AL2OBJ3 && distance >= DLOBJ3) {
				avoidOBJ->x = 10;
				avoidOBJ->y = 50;
				trajState = Via1;
				mobileRobot->numVia = 1;
			}
			else {
				trajState = Dest;
				mobileRobot->numVia = 0;
			}

		}

		HandleEvent();
	}

}

//Function used to draw all of the neccessary components and set the camera
void display()
{
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glEnable(GL_DEPTH_TEST);
	glLoadIdentity();
	setEye();
	DrawGrid(30);
	drawCameraFrame();
	HandleEvent();
	if (state == None) {
		ioFunc();
	}
	mobileRobot->initMobileRobot();
	glFlush();
	glutSwapBuffers();
}

void reshape(int width, int height)
{
	asp = (height > 0) ? (double)width / height : 1;
	glViewport(0, 0, width, height);
	project();
}


//Timer function provided by glut
//If there is an event requiring animation, will call glutPostRedisplay() rapidly for animation
//Do nothing if no state
void timer_func(int n) {
	if (state != None) {
		glutPostRedisplay();
	}
	glutTimerFunc(100, timer_func, 0);

}

//MAIN
int main(int argc, char* argv[])
{
	glutInit(&argc, argv);
	glutInitDisplayMode(GLUT_RGB | GLUT_DOUBLE | GLUT_DEPTH);
	glutInitWindowSize(windowWidth, windowHeight);
	glutCreateWindow(windowName);
	glutDisplayFunc(display);
	glutReshapeFunc(reshape);
	std::cout << "*****************************************************" << std::endl;
	std::cout << "********      Welcome to Robot Simulator     ********" << std::endl;
	std::cout << "*****************************************************" << std::endl;
	std::cout << std::endl;
	timer_func(0);
	glutMainLoop();
	return 0;
}
