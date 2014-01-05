/*
 * Stellarium
 * Copyright (C) 2010 Bogdan Marinov
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Suite 500, Boston, MA  02110-1335, USA.
 */
 
#include "Comet.hpp"
#include "Orbit.hpp"

#include "StelApp.hpp"
#include "StelCore.hpp"
#include "StelPainter.hpp"

#include "StelTexture.hpp"
#include "StelTextureMgr.hpp"
#include "StelTranslator.hpp"
#include "StelUtils.hpp"
#include "StelFileMgr.hpp"
#include "StelMovementMgr.hpp"
#include "StelModuleMgr.hpp"
#include "LandscapeMgr.hpp"

#include <QRegExp>
#include <QDebug>

Comet::Comet(const QString& englishName,
		 int flagLighting,
		 double radius,
		 double oblateness,
		 Vec3f color,
		 float albedo,
		 const QString& atexMapName,
		 posFuncType coordFunc,
		 void* auserDataPtr,
		 OsculatingFunctType *osculatingFunc,
		 bool acloseOrbit,
		 bool hidden,
		 const QString& pType,
		 float dustTailWidthFact, float dustTailLengthFact, float dustTailBrightnessFact)
	: Planet (englishName,
		  flagLighting,
		  radius,
		  oblateness,
		  color,
		  albedo,
		  atexMapName,
		  coordFunc,
		  auserDataPtr,
		  osculatingFunc,
		  acloseOrbit,
		  hidden,
		  false, //No atmosphere
		  true, //halo
		  pType),
	  dustTailWidthFactor(dustTailWidthFact),
	  dustTailLengthFactor(dustTailLengthFact),
	  dustTailBrightnessFactor(dustTailBrightnessFact)
{
	texMapName = atexMapName;
	lastOrbitJD =0;
	deltaJD = StelCore::JD_SECOND;
	orbitCached = 0;
	closeOrbit = acloseOrbit;

	eclipticPos=Vec3d(0.,0.,0.);
	rotLocalToParent = Mat4d::identity();
	texMap = StelApp::getInstance().getTextureManager().createTextureThread(StelFileMgr::getInstallationDir()+"/textures/"+texMapName, StelTexture::StelTextureParams(true, GL_LINEAR, GL_REPEAT));

	gastailVertexArr.clear();
	dusttailVertexArr.clear();
	gastailTexCoordArr.clear();
	//dusttailTexCoordArr.clear();
	gastailIndices.clear();
	//dusttailIndices.clear();
	comaVertexArr.clear();
	comaTexCoordArr.clear();

	comaTexture = StelApp::getInstance().getTextureManager().createTextureThread(StelFileMgr::getInstallationDir()+"/textures/cometComa.png", StelTexture::StelTextureParams(true, GL_LINEAR, GL_CLAMP_TO_EDGE));
	//GZ: tail textures. We use a paraboloid tail body, textured like a fisheye sphere, i.e. center=head. The texture should be something like a mottled star to give some structure.
	gasTailTexture = StelApp::getInstance().getTextureManager().createTextureThread(StelFileMgr::getInstallationDir()+"/textures/cometTail.png", StelTexture::StelTextureParams(true, GL_LINEAR, GL_CLAMP_TO_EDGE));
	// GZ: I think we need only one texture for the tails.
	//dusttailTexture = StelApp::getInstance().getTextureManager().createTextureThread(StelFileMgr::getInstallationDir()+"/textures/cometTail.png", StelTexture::StelTextureParams(true, GL_LINEAR, GL_CLAMP_TO_EDGE));

	//Comet specific members
	absoluteMagnitude = 0;
	slopeParameter = -1;//== uninitialized: used in getVMagnitude()

	//TODO: Name processing?

	nameI18 = englishName;

	flagLabels = true;

}

Comet::~Comet()
{
}

void Comet::setAbsoluteMagnitudeAndSlope(const double magnitude, const double slope)
{
	if (slope < 0 || slope > 20.0)
	{
		qDebug() << "Comet::setAbsoluteMagnitudeAndSlope(): Invalid slope parameter value (must be between 0 and 20)";
		return;
	}

	//TODO: More checks?
	//TODO: Make it set-once like the number?

	absoluteMagnitude = magnitude;
	slopeParameter = slope;
}

QString Comet::getInfoString(const StelCore *core, const InfoStringGroup &flags) const
{
	//Mostly copied from Planet::getInfoString():
	QString str;
	QTextStream oss(&str);

	if (flags&Name)
	{
		oss << "<h2>";
		oss << q_(englishName);  // UI translation can differ from sky translation
		oss.setRealNumberNotation(QTextStream::FixedNotation);
		oss.setRealNumberPrecision(1);
		if (sphereScale != 1.f)
			oss << QString::fromUtf8(" (\xC3\x97") << sphereScale << ")";
		oss << "</h2>";
	}

	if (flags&Type)
	{
		if (pType.length()>0)
			oss << q_("Type: <b>%1</b>").arg(q_(pType)) << "<br />";
	}

	if (flags&Magnitude)
	{
	    if (core->getSkyDrawer()->getFlagHasAtmosphere())
		oss << q_("Magnitude: <b>%1</b> (extincted to: <b>%2</b>)").arg(QString::number(getVMagnitude(core), 'f', 2),
									    QString::number(getVMagnitudeWithExtinction(core), 'f', 2)) << "<br>";
	    else
		oss << q_("Magnitude: <b>%1</b>").arg(getVMagnitude(core), 0, 'f', 2) << "<br>";
	}

	if (flags&AbsoluteMagnitude)
	{
		//TODO: Make sure absolute magnitude is a sane value
		//If the two parameter magnitude system is not use, don't display this
		//value. (Using radius/albedo doesn't make any sense for comets.)
		if (slopeParameter >= 0)
			oss << q_("Absolute Magnitude: %1").arg(absoluteMagnitude, 0, 'f', 2) << "<br>";
	}

	oss << getPositionInfoString(core, flags);

	if (flags&Distance)
	{
		// GZ: Distance from sun should be added to all planets IMHO.
		double distanceAu = getHeliocentricEclipticPos().length();
		double distanceKm = AU * distanceAu;
		if (distanceAu < 0.1)
		{
			// xgettext:no-c-format
			oss << QString(q_("Distance from Sun: %1AU (%2 km)"))
				   .arg(distanceAu, 0, 'f', 6)
				   .arg(distanceKm, 0, 'f', 3);
		}
		else
		{
			// xgettext:no-c-format
			oss << QString(q_("Distance from Sun: %1AU (%2 Mio km)"))
				   .arg(distanceAu, 0, 'f', 3)
				   .arg(distanceKm / 1.0e6, 0, 'f', 3);
		}
		oss << "<br>";
		distanceAu = getJ2000EquatorialPos(core).length();
		distanceKm = AU * distanceAu;
		if (distanceAu < 0.1)
		{
			// xgettext:no-c-format
			oss << QString(q_("Distance: %1AU (%2 km)"))
				   .arg(distanceAu, 0, 'f', 6)
				   .arg(distanceKm, 0, 'f', 3);
		}
		else
		{
			// xgettext:no-c-format
			oss << QString(q_("Distance: %1AU (%2 Mio km)"))
				   .arg(distanceAu, 0, 'f', 3)
				   .arg(distanceKm / 1.0e6, 0, 'f', 3);
		}
		oss << "<br>";
		// GZ: Add speed. I don't know where to place that bit of information.
		// xgettext:no-c-format
		oss << QString(q_("Speed: %1 km/s"))
			   .arg(((CometOrbit*)userDataPtr)->getVelocity().length()*AU/86400.0, 0, 'f', 3);
		oss << "<br>";
	}

	/*
	if (flags&Size)
		oss << q_("Apparent diameter: %1").arg(StelUtils::radToDmsStr(2.*getAngularSize(core)*M_PI/180., true));
	*/

	// If semi-major axis not zero then calculate and display orbital period for comet in days
	double siderealPeriod = getSiderealPeriod();
	if ((flags&Extra) && (siderealPeriod>0))
	{
		// TRANSLATORS: Sidereal (orbital) period for solar system bodies in days and in Julian years (symbol: a)
		oss << q_("Sidereal period: %1 days (%2 a)").arg(QString::number(siderealPeriod, 'f', 2)).arg(QString::number(siderealPeriod/365.25, 'f', 3)) << "<br>";
	}

	postProcessInfoString(str, flags);

	return str;
}

void Comet::setSemiMajorAxis(const double value)
{
	semiMajorAxis = value;
}

double Comet::getSiderealPeriod() const
{
	double period;
	if (semiMajorAxis>0)
		period = StelUtils::calculateSiderealPeriod(semiMajorAxis);
	else
		period = 0;

	return period;
}

float Comet::getVMagnitude(const StelCore* core) const
{
	//If the two parameter system is not used,
	//use the default radius/albedo mechanism
	if (slopeParameter < 0)
	{
		return Planet::getVMagnitude(core);
	}

	//Calculate distances
	const Vec3d& observerHeliocentricPosition = core->getObserverHeliocentricEclipticPos();
	const Vec3d& cometHeliocentricPosition = getHeliocentricEclipticPos();
	const double cometSunDistance = cometHeliocentricPosition.length();
	const double observerCometDistance = (observerHeliocentricPosition - cometHeliocentricPosition).length();

	//Calculate apparent magnitude
	//Sources: http://www.clearskyinstitute.com/xephem/help/xephem.html#mozTocId564354
	//(XEphem manual, section 7.1.2.3 "Magnitude models"), also
	//http://www.ayton.id.au/gary/Science/Astronomy/Ast_comets.htm#Comet%20facts:
	// GZ: Note that Meeus, Astr.Alg.1998 p.231, has m=absoluteMagnitude+5log10(observerCometDistance) + kappa*log10(cometSunDistance)
	// with kappa typically 5..15. MPC provides Slope parameter. So we should expect to have slopeParameter (a word only used for minor planets!) for our comets 2..6
	double apparentMagnitude = absoluteMagnitude + 5 * std::log10(observerCometDistance) + 2.5 * slopeParameter * std::log10(cometSunDistance);

	return apparentMagnitude;
}

// Draw the Comet and all the related infos : name, circle etc... GZ: Taken from Planet.cpp 2013-11-05 and extended
void Comet::draw(StelCore* core, float maxMagLabels, const QFont& planetNameFont)
{
	if (hidden)
		return;
	if (getEnglishName() == core->getCurrentLocation().planetName)
	{ // GZ moved this up. Maybe even don't do that? E.g., draw tail while riding the comet? Decide later.
		return;
	}

	// The CometOrbit is in fact available in userDataPtr!
	CometOrbit* orbit=(CometOrbit*)userDataPtr;
	Q_ASSERT(orbit);
	if (!orbit->objectDateValid(core->getJDay())) return; // out of useful date range. This allows having hundreds of comet elements.

	if (orbit->getUpdateTails()){
		// Compute lengths and orientations from orbit object, but only if required.
		// TODO: This part should possibly be moved to another thread to keep draw() free from too much computation.

		Vec2f tailFactors=getComaDiameterAndTailLengthAU();
		float gasparameter=tailFactors[0]*tailFactors[0]/(2.0f*tailFactors[1]); // parabola formula: z=r²/2p, so p=r²/2z
		// The dust tail is thicker and usually shorter. The factors can be configured in the elements.
		float dustparameter=tailFactors[0]*tailFactors[0]*dustTailWidthFactor*dustTailWidthFactor/(2.0f*dustTailLengthFactor*tailFactors[1]);
		// TODO: Influence of H10?  --> are included...

		// TODO: find valid parameters to create paraboloid vertex arrays: dustTail, gasTail.
		computeParabola(gasparameter, tailFactors[0], -0.5f*gasparameter, 16, 16, gastailVertexArr,  gastailTexCoordArr, gastailIndices);
		//computeParabola(dustparameter, 2.0f*tailFactors[0], -0.5f*dustparameter,  16, 16, dusttailVertexArr, dusttailTexCoordArr, dusttailIndices);
		computeParabola(dustparameter, dustTailWidthFactor*tailFactors[0], -0.5f*dustparameter,  16, 16, dusttailVertexArr, gastailTexCoordArr, gastailIndices);

		// Note that we use a diameter larger than what the formula returns. A scale factor of 1.2 is ad-hoc/empirical (GZ), but may look better.
		computeComa(1.0f*tailFactors[0]);
		orbit->setUpdateTails(false); // don't update until position has been recalculated elsewhere
	}

	Mat4d mat = Mat4d::translation(eclipticPos) * rotLocalToParent;
/*  // We can remove that - a Comet has no parent except for the sun...
	PlanetP p = parent;
	while (p && p->parent)
	{
		mat = Mat4d::translation(p->eclipticPos) * mat * p->rotLocalToParent;
		p = p->parent;
	}
*/
	// This removed totally the Planet shaking bug!!!
	StelProjector::ModelViewTranformP transfo = core->getHeliocentricEclipticModelViewTransform();
	transfo->combine(mat);

	// Compute the 2D position and check if in the screen
	const StelProjectorP prj = core->getProjection(transfo);
	float screenSz = getAngularSize(core)*M_PI/180.*prj->getPixelPerRadAtCenter();
	float viewport_left = prj->getViewportPosX();
	float viewport_bottom = prj->getViewportPosY();
	if (prj->project(Vec3d(0), screenPos)
		&& screenPos[1]>viewport_bottom - screenSz && screenPos[1] < viewport_bottom + prj->getViewportHeight()+screenSz
		&& screenPos[0]>viewport_left - screenSz && screenPos[0] < viewport_left + prj->getViewportWidth() + screenSz)
	{
		// Draw the name, and the circle if it's not too close from the body it's turning around
		// this prevents name overlapping (ie for jupiter satellites)
		float ang_dist = 300.f*atan(getEclipticPos().length()/getEquinoxEquatorialPos(core).length())/core->getMovementMgr()->getCurrentFov();
		// if (ang_dist==0.f) ang_dist = 1.f; // if ang_dist == 0, the Planet is sun.. --> GZ: we can remove it.

		// by putting here, only draw orbit if Comet is visible for clarity
		drawOrbit(core);  // TODO - fade in here also...

		if (flagLabels && ang_dist>0.25 && maxMagLabels>getVMagnitude(core))
		{
			labelsFader=true;
		}
		else
		{
			labelsFader=false;
		}
		drawHints(core, planetNameFont);

		draw3dModel(core,transfo,screenSz);
	}
	// tails should also be drawn if core is off-screen...
	drawTail(core,transfo,true);  // gas tail
	drawTail(core,transfo,false); // dust tail

	//Coma: this is just a fan disk tilted towards the observer;-)
	drawComa(core, transfo);
	return;
}

void Comet::drawTail(StelCore* core, StelProjector::ModelViewTranformP transfo, bool gas)
{

	// Find rotation matrix from 0/0/1 to eclipticPosition: crossproduct for axis (normal vector), dotproduct for angle.
	Vec3d eclposNrm=eclipticPos; eclposNrm.normalize();
	Mat4d tailrot=Mat4d::rotation(Vec3d(0.0, 0.0, 1.0)^(eclposNrm), std::acos(Vec3d(0.0, 0.0, 1.0).dot(eclposNrm)) );

	StelProjector::ModelViewTranformP transfo2 = transfo->clone();
	transfo2->combine(tailrot);
	if (!gas) {
		CometOrbit* orbit=(CometOrbit*)userDataPtr;
		Vec3d velocity=orbit->getVelocity(); // [AU/d]
		Mat4d dustTailRot=Mat4d::rotation(eclposNrm^(-velocity), 0.15f*std::acos(eclposNrm.dot(-velocity))); // GZ: This scale factor of 0.15 is empirical from photos of Halley and Hale-Bopp.
		transfo2->combine(dustTailRot);
	}
	//transfo2->combine(Mat4d::yrotation(M_PI/2)); // From along Z axis towards along X axis
	StelPainter* sPainter = new StelPainter(core->getProjection(transfo2));
	sPainter->getLight().disable();
	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE);
	glDisable(GL_CULL_FACE);

	// GZ: If we use getVMagnitudeWithExtinction(), a head extincted in the horizon mist can completely hide an otherwise frighteningly long tail.
	// we must use unextincted mag, but mix/dim with atmosphere/sky brightness.
	// In addition, light falloff is a bit reduced for better visibility. Power basis should be 0.4, we use 0.6.
	//float magfactor=std::pow(0.4f , getVMagnitudeWithExtinction(core));
	float magFactor=std::pow(0.6f , getVMagnitude(core));
	if (core->getSkyDrawer()->getFlagHasAtmosphere())
	{
		// Mix with sky brightness and light pollution: This is very ad-hoc, if someone finds a better solution, please go ahead!
		// Light pollution:
		float bortleIndexFactor=0.1f * (11 - core->getSkyDrawer()->getBortleScaleIndex());
		magFactor*= bortleIndexFactor*bortleIndexFactor; // GZ-Guesstimate for light pollution influence
		// sky brightness: This is about 10 for twilight where bright comet tails should already be visible. Dark night is close to 0.
		float avgAtmLum=GETSTELMODULE(LandscapeMgr)->getAtmosphereAverageLuminance();
		float atmLumFactor=(10.0f-avgAtmLum)/10.0f;  if (atmLumFactor<0.0f) atmLumFactor=0.0f;    //atmLumFactor=std::sqrt(atmLumFactor);
		magFactor*=atmLumFactor*atmLumFactor;
	}
	magFactor*=(gas? 0.9 : dustTailBrightnessFactor); // TBD: empirical adjustment for texture brightness.

	gasTailTexture->bind();

	if (gas) {
		//gasTexture->bind();
		sPainter->setColor(0.15f*magFactor,0.15f*magFactor,0.8f*magFactor);
		sPainter->setArrays((Vec3d*)gastailVertexArr.constData(), (Vec2f*)gastailTexCoordArr.constData());
		sPainter->drawFromArray(StelPainter::Triangles, gastailIndices.size(), 0, true, gastailIndices.constData());

	} else {
		//dustTexture->bind();
		sPainter->setColor(magFactor, magFactor,0.6f*magFactor);
		//sPainter->setArrays((Vec3d*)dusttailVertexArr.constData(), (Vec2f*)dusttailTexCoordArr.constData());
		//sPainter->drawFromArray(StelPainter::Triangles, dusttailIndices.size(), 0, true, dusttailIndices.constData());
		sPainter->setArrays((Vec3d*)dusttailVertexArr.constData(), (Vec2f*)gastailTexCoordArr.constData());
		sPainter->drawFromArray(StelPainter::Triangles, gastailIndices.size(), 0, true, gastailIndices.constData());
	}
	glDisable(GL_BLEND);



	if (sPainter)
		delete sPainter;
	sPainter=NULL;
}

void Comet::drawComa(StelCore* core, StelProjector::ModelViewTranformP transfo)
{

	// Find rotation matrix from 0/0/1 to viewdirection! crossproduct for axis (normal vector), dotproduct for angle.
	Vec3d eclposNrm=eclipticPos - core->getObserverHeliocentricEclipticPos()  ; eclposNrm.normalize();
	Mat4d comarot=Mat4d::rotation(Vec3d(0.0, 0.0, 1.0)^(eclposNrm), std::acos(Vec3d(0.0, 0.0, 1.0).dot(eclposNrm)) );
	StelProjector::ModelViewTranformP transfo2 = transfo->clone();
	transfo2->combine(comarot);
	StelPainter* sPainter = new StelPainter(core->getProjection(transfo2));
	sPainter->getLight().disable();

	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE);
	glDisable(GL_CULL_FACE);

	// GZ: For the coma, we can use extinction via atmosphere.
	// In addition, light falloff is a bit reduced for better visibility. Power basis should be 0.4, we use 0.6.
	float minSkyMag=core->getSkyDrawer()->getLimitMagnitude();
	float mag100pct=minSkyMag-6.0f; // should be 5, but let us draw it a bit brighter.
	float magDrop=getVMagnitudeWithExtinction(core)-mag100pct;
	float magFactor=std::pow(0.6f , magDrop);
	//float magFactor=std::pow(0.6f , getVMagnitude(core));
/*	if (core->getSkyDrawer()->getFlagHasAtmosphere())
	{
		// Mix with sky brightness and light pollution: This is very ad-hoc, if someone finds a better solution, please go ahead!
		// Light pollution:
		float bortleIndexFactor=0.1f * (11 - core->getSkyDrawer()->getBortleScaleIndex());
		magFactor*= bortleIndexFactor*bortleIndexFactor; // GZ-Guesstimate for light pollution influence
		// sky brightness: This is about 10 for twilight where bright comet tails should already be visible. Dark night is close to 0.
		float avgAtmLum=GETSTELMODULE(LandscapeMgr)->getAtmosphereAverageLuminance();
		float atmLumFactor=(10.0f-avgAtmLum)/10.0f;  if (atmLumFactor<0.0f) atmLumFactor=0.0f;    //atmLumFactor=std::sqrt(atmLumFactor);
		magFactor*=atmLumFactor*atmLumFactor;
	}
*/

	comaTexture->bind();
	sPainter->setColor(magFactor,magFactor,0.6f*magFactor);
	sPainter->setArrays((Vec3d*)comaVertexArr.constData(), (Vec2f*)comaTexCoordArr.constData());
	sPainter->drawFromArray(StelPainter::Triangles, comaVertexArr.size()/3);

	glDisable(GL_BLEND);

	if (sPainter)
		delete sPainter;
	sPainter=NULL;
}

// Formula found at http://www.projectpluto.com/update7b.htm#comet_tail_formula
Vec2f Comet::getComaDiameterAndTailLengthAU() const {
	float r=getHeliocentricEclipticPos().length();
	float mhelio=absoluteMagnitude+slopeParameter*log10(r);
	float Do=pow(10.0f, ((-0.0033f*mhelio - 0.07f)*mhelio + 3.25f));
	float D =Do*(1.0f-pow(10.0, -2.0f*r))*(1.0f-pow(10.0, -r))* (1000.0f*AU_KM);
	float Lo=pow(10.0f, ((-0.0075f*mhelio - 0.19f)*mhelio + 2.1f));
	float L = Lo*(1.0f-pow(10.0, -4.0f*r))*(1.0f-pow(10.0, -2.0f*r))* (1e6*AU_KM);
	return Vec2f(D, L);
}

void Comet::computeComa(const float diameter)
{
	StelPainter::computeFanDisk(0.5f*diameter, 3, 3, comaVertexArr, comaTexCoordArr);
}

//! create parabola shell to represent a tail. Designed for slices=16, stacks=16, but should work with other sizes as well.
//! (Maybe slices must be an even number.)
// Parabola equation: z=x²/2p.
void Comet::computeParabola(const float parameter, const float radius, const float zshift, const int slices, const int stacks,
							QVector<double>& vertexArr, QVector<float>& texCoordArr, QVector<unsigned short> &indices) {
	vertexArr.clear();
	//texCoordArr.clear();
	//indices.clear();
	bool createIndices= indices.empty();
	bool createTexcoords= texCoordArr.empty();
	int i;
	// The parabola has triangular faces with vertices on two circles that are rotated against each other. 
	float xa[2*slices];
	float ya[2*slices];
	float x, y, z;
	
	// fill xa, ya with sin/cosines. TBD: make more efficient with index mirroring etc.
	float da=M_PI/slices; // full circle/2slices
	for (i=0; i<2*slices; ++i){
		xa[i]=-sin(i*da);
		ya[i]=cos(i*da);
	}
	
	// center point, vertex0
	vertexArr << 0.0f << 0.0f << zshift;
	if (createTexcoords) texCoordArr << 0.5f << 0.5f;
	// define the indices lying on circles, starting at 1: odd rings have 1/slices+1/2slices, even-numbered rings straight 1/slices
	// inner ring#1
	int ring;
	for (ring=1; ring<=stacks; ++ring){
		z=ring*radius/stacks; z=z*z/(2*parameter) + zshift;
		for (i=ring & 1; i<2*slices; i+=2) { // i.e., ring1 has shifted vertices, ring2 has even ones.
			x=xa[i]*radius*ring/stacks;
			y=ya[i]*radius*ring/stacks;
			vertexArr << x << y << z;
			if (createTexcoords) texCoordArr << 0.5+ 0.5*x/radius << 0.5+0.5*y/radius;
		}
	}
	// now link the faces with indices. That's fun... ;-)
	if (createIndices)
	{
		for (i=1; i<slices; ++i) indices << 0 << i << i+1;
		indices << 0 << slices << 1; // close inner fan.
		// The other slices are a repeating pattern of 2 possibilities. Index @ring always is on the inner ring (slices-agon)
		for (ring=1; ring<stacks; ring+=2) { // odd rings
			const int first=(ring-1)*slices+1;
			for (i=0; i<slices-1; ++i){
				indices << first+i << first+slices+i << first+slices+1+i;
				indices << first+i << first+slices+1+i << first+1+i;
			}
			// closing slice: mesh with other indices...
			indices << ring*slices << (ring+1)*slices << ring*slices+1;
			indices << ring*slices << ring*slices+1 << first;
		}

		for (ring=2; ring<stacks; ring+=2) { // even rings: different sequence.
			const int first=(ring-1)*slices+1;
			for (i=0; i<slices-1; ++i){
				indices << first+i << first+slices+i << first+1+i;
				indices << first+1+i << first+slices+i << first+slices+1+i;
			}
			// closing slice: mesh with other indices...
			indices << ring*slices << (ring+1)*slices << first;
			indices << first << (ring+1)*slices << ring*slices+1;
		}
	}
//	qDebug() << "Parabola: Vertex index dump\n";
//	for (int i=0; i<indices.length(); i+=3)
//		qDebug() << indices[i] << "-" << indices[i+1] << "-" << indices[i+2] << ":"
//				 << vertexArr[3*indices[i]]   << vertexArr[3*indices[i]+1]   << vertexArr[3*indices[i]+2]   << "/"
//				 << vertexArr[3*indices[i+1]] << vertexArr[3*indices[i+1]+1] << vertexArr[3*indices[i+1]+2] << "/"
//				 << vertexArr[3*indices[i+2]] << vertexArr[3*indices[i+2]+1] << vertexArr[3*indices[i+2]+2];

}
