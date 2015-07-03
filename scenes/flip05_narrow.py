#
# Flip scene with particle seeding only in a narrow band around the surface
# 
from manta import *

import time

# Configuration 
simtypeno = 0
simtype = ["levelset", "flip0", "flip", "nbflip"][simtypeno]
narrowBand  = 4; # nbflip only: no. of cells around surface which contain particles
combineBand = 3; # nbflip only: no. of cells around surface which are influenced by particles
kernelType  = 1; # XflipY only: particle mapping kernel (1: d-linear, 2: smooth SPH kernel with support of 4^d grid points)
if simtype in ["levelset", "flip0", "flip"]: narrowBand = combineBand = 0
if simtype in ["levelset", "flip0"]: kernelType = 0



print("Sim Type:", simtype, end='')
if simtype == "nbflip": print(", NB", narrowBand, ", CB", combineBand, end='')
if simtype in ["flip", "nbflip"]:  print(", kernelType", kernelType, end='')
print("")

# solver params
dim = 2
res = 64
#res = 80
gs = vec3(res,res,res)
if (dim==2):
	gs.z=1
s = Solver(name='main', gridSize = gs, dim=dim)

# adaptive time stepping
s.frameLength = 2.0   # length of one frame (in "world time")
s.timestep    = 1.0
s.timestepMin = 0.2   # time step range
s.timestepMax = 2.0
#s.cfl         = 1.0   # maximal velocity per cell, 0 to use fixed timesteps

gravity = (0,-0.003,0)
targetVolume = 800

minParticles = pow(2,dim)
timings = Timings()

# size of particles 
radiusFactor = 1.0

# prepare grids and particles
flags    = s.create(FlagGrid)

phiParts = s.create(LevelsetGrid)
phi      = s.create(LevelsetGrid)
ttt      = s.create(RealGrid)
perCellCorr = s.create(RealGrid)
pressure = s.create(RealGrid)
phiBackup = s.create(RealGrid)

vel      = s.create(MACGrid)
combineDiff = s.create(MACGrid)
velOld   = s.create(MACGrid)
velParts = s.create(MACGrid)
mapWeights = s.create(MACGrid)

pp       = s.create(BasicParticleSystem) 
pVel     = pp.create(PdataVec3) 
mesh     = s.create(Mesh)

# acceleration data for particle nbs
pindex = s.create(ParticleIndexSystem) 
gpi    = s.create(IntGrid)

# scene setup, 0=breaking dam, 1=drop into pool
# geometry in world units (to be converted to grid space upon init)
setup = 1
flags.initDomain(boundaryWidth=0)
fluidVel = 0
fluidSetVel = 0

if setup==0:
	# breaking dam
	fluidbox = s.create(Box, p0=gs*vec3(0,0,0), p1=gs*vec3(0.4,0.6,1)) # breaking dam
	phi = fluidbox.computeLevelset()
	#fluidbox2 = s.create(Box, p0=gs*vec3(0.2,0.7,0.3), p1=gs*vec3(0.5,0.8,0.6)) # breaking dam
	#phi.join( fluidbox2.computeLevelset() )

	# obstacle init needs to go after updateFromLs
	obsBox = s.create(Box, p0=gs*vec3(0.7,0.0,0.5), p1=gs*vec3(0.8,1.0,0.8)) 
	obsBox.applyToGrid(grid=flags, value=(FlagObstacle) )
	#obsBox.applyToGrid(grid=flags, value=(FlagObstacle|FlagStick) )
	flags.updateFromLevelset(phi)

elif setup==1:
	# falling drop
	fluidBasin = s.create(Box, p0=gs*vec3(0,0,0), p1=gs*vec3(1.0,0.2,1.0)) # basin
	dropCenter = vec3(0.5,0.8,0.5);
	#dropCenter = vec3(0.5,0.20,0.5); # wave only
	dropRadius = 0.15
	dropRadius = 0.075 #sm
	fluidSetVel= vec3(0,-0.001,0)
	fluidDrop  = s.create(Sphere, center=gs*dropCenter, radius=res*dropRadius)
	fluidVel   = s.create(Sphere, center=gs*dropCenter, radius=res*(dropRadius+0.05) )
	phi = fluidBasin.computeLevelset()
	phi.join( fluidDrop.computeLevelset() ) 
	flags.updateFromLevelset(phi)

#if simtype in ["flip0", "flip", "nbflip"]:
sampleLevelsetWithParticles( phi=phi, flags=flags, parts=pp, discretization=2, randomness=0.05 )

if fluidVel!=0:
	# set initial velocity
	fluidVel.applyToGrid( grid=vel , value=gs*fluidSetVel )
	#mapGridToPartsVec3(source=vel, parts=pp, target=pVel )

if simtype in ["flip", "nbflip"]:
	mapGridToPartsVec3(source=vel, parts=pp, target=pVel )

if 1 and (GUI):
	gui = Gui()
	gui.show( dim==2 )
	gui.pause()
	  
	# show all particles shaded by velocity
	if(dim==2):
		gui.nextPdata()
		#gui.nextPartDisplay()
		#gui.nextPartDisplay()

lastframe = 0
statstxt = open('../vid/stats/flip05_st%i_nb%02i_cb%02i_kt%i.txt' % (simtypeno,round(narrowBand*10),round(combineBand*10),kernelType), 'w')

#main loop
while s.frame < 500:
	
	maxVel = vel.getMaxValue()
	if (s.cfl < 1000): s.adaptTimestep( maxVel )
	
	# velocities are extrapolated at the end of each step
	pp.advectInGrid(flags=flags, vel=vel, integrationMode=IntRK4, deleteInObstacle=False ) 
	advectSemiLagrange(flags=flags, vel=vel, grid=phi, order=1)
	flags.updateFromLevelset(phi)
	advectSemiLagrange(flags=flags, vel=vel, grid=vel, order=2)

	# create approximate surface level set, resample particles
	gridParticleIndex( parts=pp , flags=flags, indexSys=pindex, index=gpi )
	unionParticleLevelset( pp, pindex, flags, gpi, phiParts , radiusFactor )

	if simtype == "nbflip":
		phi.addConst(1.); # shrink slightly
		phi.join( phiParts );
		extrapolateLsSimple(phi=phi, distance=narrowBand+2, inside=True)
	elif simtype in ["flip0", "flip"]:
		phi.copyFrom( phiParts );
		extrapolateLsSimple(phi=phi, distance=4, inside=True)
		
	if simtype in ["flip0", "flip", "nbflip"]:
		extrapolateLsSimple(phi=phi, distance=3)
		flags.updateFromLevelset(phi)

	# make sure we have velocities throught liquid region
	if simtype == "nbflip":
		mapPartsToMAC(vel=velParts, flags=flags, velOld=velOld, parts=pp, partVel=pVel, weight=mapWeights, kernelType=kernelType );
		combineDiff.copyFrom(vel)
		combineDiff.multConst(vec3(-1))
		combineGridVel(vel=velParts, weight=mapWeights , combineVel=vel, phi=phi, narrowBand=combineBand, thresh=0.1)
		combineDiff.add(vel)
		velOld.copyFrom(vel)
	elif simtype == "flip":
		mapPartsToMAC(vel=vel, flags=flags, velOld=velOld, parts=pp, partVel=pVel, weight=mapWeights, kernelType=kernelType  );
		
	# forces & pressure solve
	addGravity(flags=flags, vel=vel, gravity=gravity)
	setWallBcs(flags=flags, vel=vel)
	vol = calcFluidVolume(flags)
	perCellCorr.setConst(0.01 * ( targetVolume-vol ) / vol)
	solvePressure(flags=flags, vel=vel, pressure=pressure, phi=phi, perCellCorr=perCellCorr)
	#solvePressure(flags=flags, vel=vel, pressure=pressure)
	setWallBcs(flags=flags, vel=vel)

	# make sure we have proper velocities for levelset advection
	if simtype in ["flip", "nbflip"]:
		extrapolateMACSimple( flags=flags, vel=vel, distance=(int(maxVel*1.25 + 2.)) )
		#extrapolateMACFromWeight( vel=vel , distance=(int(maxVel*1.1 + 2.)), weight=tmpVec3 ) 
	elif simtype in ["levelset", "flip0"]:
		phiBackup.copyFrom(phi)
		phi.reinitMarching(flags=flags, velTransport=vel, ignoreWalls=False);	
		phi.copyFrom(phiBackup);
		phi.reinitExact(flags=flags)

	if simtype in ["flip", "nbflip"]:	
		#flipVelocityUpdate(vel=vel, velOld=velOld, flags=flags, parts=pp, partVel=pVel, flipRatio=0.95 )
		#flipVelocityUpdateNb(vel=vel, velOld=velOld, flags=flags, parts=pp, partVel=pVel, flipRatio=0.95, narrowBand=narrowBand , phi=phi, test=ttt )
		flipVelocityUpdate(vel=vel, velOld=velOld, flags=flags, parts=pp, partVel=pVel, flipRatio=0.95 )

	# set source grids for resampling, used in adjustNumber!
	if simtype in ["flip", "nbflip"]:
		pVel.setSource( vel, isMAC=True )
	else:
		pVel.setSource( 0, isMAC=False )

	#kin = calcKineticEnergy(flags,vel)
	vol = calcFluidVolume(flags)
	nrg = calcTotalEnergy(flags,vel,gravity)
	print(vol)
	print(vol)

	if 1 and (dim==3):
		phi.createMesh(mesh)

	if 1: 
	#if 1 and s.frame < 2: 
	#if (s.frame<2): #or s.frame%100==20):
		if simtype == "levelset":
			adjustNumber( parts=pp, vel=vel, flags=flags, minParticles=1*minParticles, maxParticles=2*minParticles, phi=phi, radiusFactor=radiusFactor , narrowBand=narrowBand ) 
		else:
			adjustNumber( parts=pp, vel=vel, flags=flags, minParticles=1*minParticles, maxParticles=2*minParticles, phi=phi, radiusFactor=radiusFactor ) 

	#timings.display()
	#s.printMemInfo()
	s.step()
		
	# optionally particle data , or screenshot, or stats
	if 1 and (GUI) and s.frame!=lastframe:
		gui.screenshot( '../vid/frames/flip05_st%i_nb%02i_cb%02i_kt%i_%04d.png' % (simtypeno,round(narrowBand*10),round(combineBand*10),kernelType, s.frame) )
		#pp.save( 'flipParts_%04d.uni' % s.frame );
		#statstxt.write('%.6f %i\n' % (nrg,vol))
		lastframe = s.frame

statstxt.close()
	

	



