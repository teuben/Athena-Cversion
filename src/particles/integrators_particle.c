#include "../copyright.h"
/*=============================================================================
FILE: integrators_particle.c
PURPOSE: provide three kinds of particle integrators, namely, 2nd order explicit,
2nd order semi-implicit and 2nd order fully implicit.

CONTAINS PUBLIC FUNCTIONS:
void integrate_particle_exp(Grid *pG);
void integrate_particle_semimp(Grid *pG);
void integrate_particle_fulimp(Grid *pG);
void feedback_predictor(Grid* pG);
void feedback_corrector(Grid *pG, Grain *gri, Grain *grf, Vector cell1, Real dv1, Real dv2, Real dv3);

History:
Written by Xuening Bai, Mar.2009

==============================================================================*/
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "../defs.h"
#include "../athena.h"
#include "../prototypes.h"
#include "prototypes.h"
#include "particle.h"
#include "../globals.h"

#ifdef PARTICLES         /* endif at the end of the file */

#ifdef SHEARING_BOX
extern Real vshear;					/* shear velocity */
#endif /* SHEARING_BOX */

/*=========================== PROTOTYPES OF PRIVATE FUNCTIONS ===============================*/

void   Delete_Ghost(Grid *pG);
Vector Get_Drag(Grid *pG, int type, Real x1, Real x2, Real x3, Real v1, Real v2, Real v3, Vector cell1, Real *tstop1);
Vector Get_Force(Grid *pG, Real x1, Real x2, Real x3, Real v1, Real v2, Real v3);


/*----------------------------------------------------------------------------------------*/
/*================================= PUBLIC FUNCTIONS =====================================*/
/*----------------------------------------------------------------------------------------
   Contains 3 integrators. They differ only in the first FOUR steps.
   Steps 5,6,7 are the same for all integrators.
 */

/* --------------------- 2nd order fully implicit particle integrator ------------------------------
   Input: 
     pG: grid which is already evolved in the predictor step. The
         particles are unevolved.
   Output:
     pG: particle velocity updated with one full time step.
         feedback force is also calculated for the corrector step.
*/
void integrate_particle_fulimp(Grid *pG)
{
  Grain *curG, *curP, mygr;	/* pointer of the current working position */
  long p;			/* particle index */
  Real x1n, x2n, x3n;		/* first order new position at half a time step */
  Real dv1, dv2, dv3;		/* amount of velocity update */
  Vector fd, fr;		/* drag force and other forces */
  Vector fc, fp, ft;		/* force at current and predictor position, total force */
  Vector cell1;			/* one over dx1, dx2, dx3 */
  Real ts11, ts12;		/* 1/stopping time */
  Real b0,A,B,C,D,Det1;		/* matrix elements and determinant */
#ifdef SHEARING_BOX
  Real oh, oh2;			/* Omega*dt and its square */
  Real yshift_frac;		/* fractional shift in the boundary (in cell unit) */
#endif
#ifdef FEEDBACK
  Vector fb;			/* feedback force */
#endif

  /*-------------------- Initialization --------------------*/
#ifdef FEEDBACK
  feedback_clear(pG);	/* clean the feedback array */
#endif /* FEEDBACK */

  curP = &(mygr);	/* temperory particle */

  /* cell1 is a shortcut expressions as well as dimension indicator */
  if (pG->Nx1 > 1)  cell1.x1 = 1.0/pG->dx1;  else cell1.x1 = 0.0;
  if (pG->Nx2 > 1)  cell1.x2 = 1.0/pG->dx2;  else cell1.x2 = 0.0;
  if (pG->Nx3 > 1)  cell1.x3 = 1.0/pG->dx3;  else cell1.x3 = 0.0;

#ifdef SHEARING_BOX
  /* fractional shift in cell unit at the x1 boundary */
  yshift_frac = fmod(vshear*pG->time, pG->dx1);
  if (yshift_frac > 0.5)
    yshift_frac -= 1.0;
#endif

  /* delete all ghost particles */
  Delete_Ghost(pG);

  /*-----------------------Main loop over all the particles---------------------*/
  p = 0;

  while (p<pG->nparticle)
  {/* loop over all particles */
    curG = &(pG->particle[p]);

    /* step 1: predictor of the particle position after one time step */
    if (pG->Nx1 > 1)  x1n = curG->x1+curG->v1*pG->dt;
    else x1n = curG->x1;
    if (pG->Nx2 > 1)  x2n = curG->x2+curG->v2*pG->dt;
    else x2n = curG->x2;
    if (pG->Nx3 > 1)  x3n = curG->x3+curG->v3*pG->dt;
    else x3n = curG->x3;

#ifdef SHEARING_BOX
#ifndef FARGO
    if (pG->Nx3 > 1) x2n -= 0.75*curG->v1*SQR(pG->dt); /* advection part */
#endif
#endif

    /* step 2: calculate the force at current position */
    fd = Get_Drag(pG, curG->property, curG->x1, curG->x2, curG->x3, curG->v1, curG->v2, curG->v3, cell1, &ts11);

    fr = Get_Force(pG, curG->x1, curG->x2, curG->x3, curG->v1, curG->v2, curG->v3);

    fc.x1 = fd.x1+fr.x1;
    fc.x2 = fd.x2+fr.x2;
    fc.x3 = fd.x3+fr.x3;

    /* step 3: calculate the force at the predicted positoin */
    fd = Get_Drag(pG, curG->property, x1n, x2n, x3n, curG->v1, curG->v2, curG->v3, cell1, &ts12);

    fr = Get_Force(pG, x1n, x2n, x3n, curG->v1, curG->v2, curG->v3);

    fp.x1 = fd.x1+fr.x1;
    fp.x2 = fd.x2+fr.x2;
    fp.x3 = fd.x3+fr.x3;

    /* step 4: calculate the velocity update */
    /* shortcut expressions */
    b0 = 1.0+pG->dt*ts11;

    /* Total force */
    ft.x1 = 0.5*(fc.x1+b0*fp.x1);
    ft.x2 = 0.5*(fc.x2+b0*fp.x2);
    ft.x3 = 0.5*(fc.x3+b0*fp.x3);

#ifdef SHEARING_BOX
    oh = Omega*pG->dt;
    if (pG->Nx3 > 1) {/* 3D shearing sheet (x1,x2,x3)=(X,Y,Z) */
      ft.x1 += -oh*fp.x2;
    #ifdef FARGO
      ft.x2 += 0.25*oh*fp.x1;
    #else
      ft.x2 += oh*fp.x1;
    #endif
    } else {         /* 2D shearing sheet (x1,x2,x3)=(X,Z,Y) */
      ft.x1 += -oh*fp.x3;
      ft.x3 += oh*fp.x1;
    }
#endif /* SHEARING_BOX */

    /* calculate the inverse matrix elements */
    D = 1.0+0.5*pG->dt*(ts11 + ts12 + pG->dt*ts11*ts12);
#ifdef SHEARING_BOX
    oh2 = SQR(oh);
    B = oh * (-2.0-(ts11+ts12)*pG->dt);
#ifdef FARGO
    A = D - 0.5*oh2;
    C = -0.25*B;
#else /* FARGO */
    A = D - 2.0*oh2;
    C = -B;
#endif /* FARGO */
    Det1 = 1.0/(SQR(A)-B*C);
    if (pG->Nx3>1) {
      dv1 = pG->dt*Det1*(ft.x1*A-ft.x2*B);
      dv2 = pG->dt*Det1*(-ft.x1*C+ft.x2*A);
      dv3 = pG->dt*ft.x3/D;
    } else {
      dv1 = pG->dt*Det1*(ft.x1*A-ft.x3*B);
      dv3 = pG->dt*Det1*(-ft.x1*C+ft.x3*A);
      dv2 = pG->dt*ft.x2/D;
    }
#else /* SHEARING_BOX */
    D = 1.0/D;
    dv1 = pG->dt*ft.x1*D;
    dv2 = pG->dt*ft.x2*D;
    dv3 = pG->dt*ft.x3*D;
#endif /* SHEARING_BOX */

    /* Step 5: particle update to curP */
    /* velocity update */
    curP->v1 = curG->v1 + dv1;
    curP->v2 = curG->v2 + dv2;
    curP->v3 = curG->v3 + dv3;

    /* position update */
    if (pG->Nx1 > 1)
      curP->x1 = curG->x1 + 0.5*pG->dt*(curG->v1+curP->v1);
    else /* do not move if this dimension collapses */
      curP->x1 = curG->x1;

    if (pG->Nx2 > 1)
      curP->x2 = curG->x2 + 0.5*pG->dt*(curG->v2+curP->v2);
    else /* do not move if this dimension collapses */
      curP->x2 = curG->x2;

    if (pG->Nx3 > 1)
      curP->x3 = curG->x3 + 0.5*pG->dt*(curG->v3+curP->v3);
    else /* do not move if this dimension collapses */
      curP->x3 = curG->x3;

#ifdef FARGO
    /* shift = -3/2 * Omega * x * dt */
    curG->shift = -0.75*Omega*(curG->x1+curP->x1)*pG->dt;
#endif

    curP->property = curG->property;

    /* step 6: calculate feedback force to the gas */
#ifdef FEEDBACK
    feedback_corrector(pG, curG, curP, cell1, dv1, dv2, dv3);
#endif /* FEEDBACK */

    /* step 7: update the particle in pG */
#ifndef FARGO
    /* if it crosses the grid boundary, mark it as a crossing out particle */
    if ((curP->x1>=x1upar) || (curP->x1<x1lpar) || (curP->x2>=x2upar) || (curP->x2<x2lpar) || (curP->x3>=x3upar) || (curP->x3<x3lpar))
#else
    /* FARGO will naturally return the "crossing out" particles in the x2 direction to the grid */
    if ((curP->x1>=x1upar) || (curP->x1<x1lpar) || (curP->x3>=x3upar) || (curP->x3<x3lpar))
#endif
        curG->pos = 10;

    /* update the particle */
    curG->x1 = curP->x1;
    curG->x2 = curP->x2;
    curG->x3 = curP->x3;
    curG->v1 = curP->v1;
    curG->v2 = curP->v2;
    curG->v3 = curP->v3;
    p++;

  } /* end of the for loop */

  /* output the status */
  ath_pout(0, "In processor %d, there are %ld particles.\n", pG->my_id, pG->nparticle);

  return;
}


/* --------------------- 2nd order semi-implicit particle integrator ------------------------------
   Input: 
     pG: grid which is already evolved in the predictor step. The
         particles are unevolved.
   Output:
     pG: particle velocity updated with one full time step.
         feedback force is also calculated for the corrector step.
*/
void integrate_particle_semimp(Grid *pG)
{
  /* loca variables */
  Grain *curG, *curP, mygr;	/* pointer of the current working position */
  long p;			/* particle index */
  Real dv1, dv2, dv3;		/* particle velocity derivatives */
  Vector fd, fr, ft;		/* drag force and other forces, total force */
  Vector cell1;			/* one over dx1, dx2, dx3 */
  Real ts1, b, b2;		/* other shortcut expressions */
  Real x1n, x2n, x3n;		/* first order new position at half a time step */
#ifdef SHEARING_BOX
  Real b1, oh;			/* Omega*h */
#endif
#ifdef FEEDBACK
  Vector fb;			/* feedback force */
#endif

  /*----------------------------Initialization---------------------------*/
#ifdef FEEDBACK
  feedback_clear(pG);	/* clean the feedback array */
#endif /* FEEDBACK */

  curP = &(mygr);	/* temperory particle */

  /* cell1 is a shortcut expressions as well as dimension indicator */
  if (pG->Nx1 > 1)  cell1.x1 = 1.0/pG->dx1;  else cell1.x1 = 0.0;
  if (pG->Nx2 > 1)  cell1.x2 = 1.0/pG->dx2;  else cell1.x2 = 0.0;
  if (pG->Nx3 > 1)  cell1.x3 = 1.0/pG->dx3;  else cell1.x3 = 0.0;

  /* delete all ghost particles */
  Delete_Ghost(pG);

  /*---------------------Main loop over all the particles-------------------*/
  p = 0;

  while (p<pG->nparticle)
  {/* loop over all particles */
    curG = &(pG->particle[p]);

    /* step 1: predictor of the particle position after half a time step */
    if (pG->Nx1 > 1)  x1n = curG->x1+0.5*curG->v1*pG->dt;
    else x1n = curG->x1;
    if (pG->Nx2 > 1)  x2n = curG->x2+0.5*curG->v2*pG->dt;
    else x2n = curG->x2;
    if (pG->Nx3 > 1)  x3n = curG->x3+0.5*curG->v3*pG->dt;
    else x3n = curG->x3;

#ifdef SHEARING_BOX
#ifndef FARGO
    if (pG->Nx3 > 1) x2n -= 0.1875*curG->v1*SQR(pG->dt); /* advection part */
#endif
#endif

    /* Step 2: interpolation to get fluid density, velocity and the sound speed at predicted position */
    fd = Get_Drag(pG, curG->property, x1n, x2n, x3n, curG->v1, curG->v2, curG->v3, cell1, &ts1);

    fr = Get_Force(pG, x1n, x2n, x3n, curG->v1, curG->v2, curG->v3);

    ft.x1 = fd.x1+fr.x1;
    ft.x2 = fd.x2+fr.x2;
    ft.x3 = fd.x3+fr.x3;

    /* step 3: (not needed) */

    /* step 4: calculate velocity update */

    /* shortcut expressions */
    b = pG->dt*ts1+2.0;
#ifdef SHEARING_BOX
    oh = Omega*pG->dt;
#ifdef FARGO
    b1 = 1.0/(SQR(b)+SQR(oh));
#else
    b1 = 1.0/(SQR(b)+4.0*SQR(oh));
#endif /* FARGO */
    b2 = b*b1;
#else
    b2 = 1.0/b;
#endif /* SHEARING BOX */

    /* velocity evolution */
#ifdef SHEARING_BOX
    if (pG->Nx3>1)
    {/* 3D shearing sheet (x1,x2,x3)=(X,Y,Z) */
      dv1 = pG->dt*2.0*b2*ft.x1 + pG->dt*4.0*oh*b1*ft.x2;
      dv2 = pG->dt*2.0*b2*ft.x2;
    #ifdef FARGO
      dv2 -= pG->dt*oh*b1*ft.x1;
    #else
      dv2 -= 4.0*pG->dt*oh*b1*ft.x1;
    #endif /* FARGO */
      dv3 = pG->dt*2.0*ft.x3/b;
    }
    else
    {/* 2D shearing sheet (x1,x2,x3)=(X,Z,Y) */
      dv1 = pG->dt*2.0*b2*ft.x1 + pG->dt*4.0*oh*b1*ft.x3;
      dv2 = pG->dt*2.0*ft.x2/b;
      dv3 = pG->dt*2.0*b2*ft.x3 - 4.0*pG->dt*oh*b1*ft.x1;
    }
#else
    dv1 = pG->dt*2.0*b2*ft.x1;
    dv2 = pG->dt*2.0*b2*ft.x2;
    dv3 = pG->dt*2.0*b2*ft.x3;
#endif /* SHEARING_BOX */

    /* Step 5: particle update to curP */
    /* velocity update */
    curP->v1 = curG->v1 + dv1;
    curP->v2 = curG->v2 + dv2;
    curP->v3 = curG->v3 + dv3;

    /* position update */
    if (pG->Nx1 > 1)
      curP->x1 = curG->x1 + 0.5*pG->dt*(curG->v1 + curP->v1);
    else /* do not move if this dimension collapses */
      curP->x1 = curG->x1;

    if (pG->Nx2 > 1)
      curP->x2 = curG->x2 + 0.5*pG->dt*(curG->v2 + curP->v2);
    else /* do not move if this dimension collapses */
      curP->x2 = curG->x2;

    if (pG->Nx3 > 1)
      curP->x3 = curG->x3 + 0.5*pG->dt*(curG->v3 + curP->v3);
    else /* do not move if this dimension collapses */
      curP->x3 = curG->x3;

#ifdef FARGO
    /* shift = -3/2 * Omega * x * dt */
    curG->shift = -0.75*Omega*(curG->x1+curP->x1)*pG->dt;
#endif

    /* step 6: calculate feedback force to the gas */
#ifdef FEEDBACK
    feedback_corrector(pG, curG, curP, cell1, dv1, dv2, dv3);
#endif /* FEEDBACK */

    /* step 7: update the particle in pG */
#ifndef FARGO
    /* if it crosses the grid boundary, mark it as a crossing out particle */
    if ((curP->x1>=x1upar) || (curP->x1<x1lpar) || (curP->x2>=x2upar) || (curP->x2<x2lpar) || (curP->x3>=x3upar) || (curP->x3<x3lpar))
#else
    /* FARGO will naturally return the "crossing out" particles in the x2 direction to the grid */
    if ((curP->x1>=x1upar) || (curP->x1<x1lpar) || (curP->x3>=x3upar) || (curP->x3<x3lpar))
#endif
        curG->pos = 10;

    /* update the particle */
    curG->x1 = curP->x1;
    curG->x2 = curP->x2;
    curG->x3 = curP->x3;
    curG->v1 = curP->v1;
    curG->v2 = curP->v2;
    curG->v3 = curP->v3;
    p++;

  }/* end of the while loop */

  /* write to log file */
  ath_pout(0, "In processor %d, there are %ld particles.\n", pG->my_id, pG->nparticle);	/* level? */

  return;
}


/* --------------------- 2nd order explicit particle integrator ------------------------------
   Input: 
     pG: grid which is already evolved in the predictor step. The
         particles are unevolved.
   Output:
     pG: particle velocity updated with one full time step.
         feedback force is also calculated for the corrector step.
*/
void integrate_particle_exp(Grid *pG)
{
  /* local variables */
  Grain *curG, *curP, mygr;	/* pointer of the current working position */
  long p;			/* particle index */
  Real dv1, dv2, dv3;		/* amount of velocity update */
  Vector fd, fr, ft;		/* drag force and other forces, total force */
  Vector cell1;			/* one over dx1, dx2, dx3 */
  Real ts1;			/* 1/stopping time */
  Real x1n, x2n, x3n;		/* first order new position at half a time step */
  Real v1n, v2n, v3n;		/* first order new velocity at half a time step */
#ifdef FEEDBACK
  Vector fb;			/* feedback force */
#endif

  /*-------------------- Initialization --------------------*/
#ifdef FEEDBACK
  feedback_clear(pG);	/* clean the feedback array */
#endif /* FEEDBACK */

  curP = &(mygr);	/* temperory particle */

  /* cell1 is a shortcut expressions as well as dimension indicator */
  if (pG->Nx1 > 1)  cell1.x1 = 1.0/pG->dx1;  else cell1.x1 = 0.0;
  if (pG->Nx2 > 1)  cell1.x2 = 1.0/pG->dx2;  else cell1.x2 = 0.0;
  if (pG->Nx3 > 1)  cell1.x3 = 1.0/pG->dx3;  else cell1.x3 = 0.0;

  /* delete all ghost particles */
  Delete_Ghost(pG);

  /*-----------------------Main loop over all the particles---------------------*/
  p = 0;

  while (p<pG->nparticle)
  {/* loop over all particles */
    curG = &(pG->particle[p]);

    /* step 1: predictor of the particle position after half a time step */
    if (pG->Nx1 > 1)  x1n = curG->x1+0.5*curG->v1*pG->dt;
    else x1n = curG->x1;
    if (pG->Nx2 > 1)  x2n = curG->x2+0.5*curG->v2*pG->dt;
    else x2n = curG->x2;
    if (pG->Nx3 > 1)  x3n = curG->x3+0.5*curG->v3*pG->dt;
    else x3n = curG->x3;

#ifdef SHEARING_BOX
#ifndef FARGO
    if (pG->Nx3 > 1) x2n -= 0.1875*curG->v1*SQR(pG->dt); /* advection part */
#endif
#endif

    /* step 2: predictor of particle velocity after half a time step */
    fd = Get_Drag(pG, curG->property, curG->x1, curG->x2, curG->x3, curG->v1, curG->v2, curG->v3, cell1, &ts1);

    fr = Get_Force(pG, curG->x1, curG->x2, curG->x3, curG->v1, curG->v2, curG->v3);

    ft.x1 = fd.x1+fr.x1;
    ft.x2 = fd.x2+fr.x2;
    ft.x3 = fd.x3+fr.x3;

    v1n = curG->v1 + 0.5*ft.x1*pG->dt;
    v2n = curG->v2 + 0.5*ft.x2*pG->dt;
    v3n = curG->v3 + 0.5*ft.x3*pG->dt;

    /* step 3: calculate the force at the predicted positoin */
    fd = Get_Drag(pG, curG->property, x1n, x2n, x3n, v1n, v2n, v3n, cell1, &ts1);

    fr = Get_Force(pG, x1n, x2n, x3n, v1n, v2n, v3n);

    ft.x1 = fd.x1+fr.x1;
    ft.x2 = fd.x2+fr.x2;
    ft.x3 = fd.x3+fr.x3;

    /* step 4: calculate velocity update */
    dv1 = ft.x1*pG->dt;
    dv2 = ft.x2*pG->dt;
    dv3 = ft.x3*pG->dt;

    /* Step 5: particle update to curP */
    /* velocity update */
    curP->v1 = curG->v1 + dv1;
    curP->v2 = curG->v2 + dv2;
    curP->v3 = curG->v3 + dv3;

    /* position update */
    if (pG->Nx1 > 1)
      curP->x1 = curG->x1 + 0.5*pG->dt*(curG->v1+curP->v1);
    else /* do not move if this dimension collapses */
      curP->x1 = curG->x1;

    if (pG->Nx2 > 1)
      curP->x2 = curG->x2 + 0.5*pG->dt*(curG->v2+curP->v2);
    else /* do not move if this dimension collapses */
      curP->x2 = curG->x2;

    if (pG->Nx3 > 1)
      curP->x3 = curG->x3 + 0.5*pG->dt*(curG->v3+curP->v3);
    else /* do not move if this dimension collapses */
      curP->x3 = curG->x3;

#ifdef FARGO
    /* shift = -3/2 * Omega * x * dt */
    curG->shift = -0.75*Omega*(curG->x1+curP->x1)*pG->dt;
#endif

    /* step 6: calculate feedback force to the gas */
#ifdef FEEDBACK
    feedback_corrector(pG, curG, curP, cell1, dv1, dv2, dv3);
#endif /* FEEDBACK */

    /* step 7: update the particle in pG */
#ifndef FARGO
    /* if it crosses the grid boundary, mark it as a crossing out particle */
    if ((curP->x1>=x1upar) || (curP->x1<x1lpar) || (curP->x2>=x2upar) || (curP->x2<x2lpar) || (curP->x3>=x3upar) || (curP->x3<x3lpar))
#else
    /* FARGO will naturally return the "crossing out" particles in the x2 direction to the grid */
    if ((curP->x1>=x1upar) || (curP->x1<x1lpar) || (curP->x3>=x3upar) || (curP->x3<x3lpar))
#endif
        curG->pos = 10;

    /* update the particle */
    curG->x1 = curP->x1;
    curG->x2 = curP->x2;
    curG->x3 = curP->x3;
    curG->v1 = curP->v1;
    curG->v2 = curP->v2;
    curG->v3 = curP->v3;
    p++;

  } /* end of the for loop */

  /* output the status */
  ath_pout(0, "In processor %d, there are %ld particles.\n", pG->my_id, pG->nparticle);

  return;
}

#ifdef FEEDBACK

/* Calculate the feedback of the drag force from the particle to the gas
   Serves for the predictor step. It deals with all the particles.
   Input: pG: grid with particles
   Output: pG: the array of drag forces exerted by the particle is updated
*/
void feedback_predictor(Grid* pG)
{
  int is,js,ks;
  long p;			/* particle index */
  Real weight[3][3][3];		/* weight function */
  Real rho, cs, tstop;		/* density, sound speed, stopping time */
  Real u1, u2, u3;
  Real vd1, vd2, vd3, vd;	/* velocity difference between particle and fluid */
  Real f1, f2, f3;		/* feedback force */
  Real m, ts1h;			/* grain mass, 0.5*dt/tstop */
  Vector cell1;			/* one over dx1, dx2, dx3 */
  Vector fb;			/* drag force, fluid velocity */
  Grain *cur;			/* pointer of the current working position */

  /* initialization */
  get_gasinfo(pG);		/* calculate gas information */

  feedback_clear(pG);		/* clean the feedback array */

  /* convenient expressions */
  if (pG->Nx1 > 1)  cell1.x1 = 1.0/pG->dx1;
  else              cell1.x1 = 0.0;

  if (pG->Nx2 > 1)  cell1.x2 = 1.0/pG->dx2;
  else              cell1.x2 = 0.0;

  if (pG->Nx3 > 1)  cell1.x3 = 1.0/pG->dx3;
  else              cell1.x3 = 0.0;

  /* loop over all particles to calculate the drag force */
  for (p=0; p<pG->nparticle; p++)
  {/* loop over all particle */
    cur = &(pG->particle[p]);

    /* interpolation to get fluid density and velocity */
    getweight(pG, cur->x1, cur->x2, cur->x3, cell1, weight, &is, &js, &ks);
    if (getvalues(pG, weight, is, js, ks, &rho, &u1, &u2, &u3, &cs) == 0)
    { /* particle is in the grid */

      /* apply gas velocity shift due to pressure gradient */
      gasvshift(cur->x1, cur->x2, cur->x3, &u1, &u2, &u3);
      /* velocity difference */
      vd1 = u1-cur->v1;
      vd2 = u2-cur->v2;
      vd3 = u3-cur->v3;
      vd = sqrt(vd1*vd1 + vd2*vd2 + vd3*vd3);

      /* calculate particle stopping time */
      tstop = MAX(get_ts(pG, cur->property, rho, cs, vd), pG->dt); /* to avoid the stiff dependence on tstop */
      ts1h = 0.5*pG->dt/tstop;

      /* Drag force density */
      m = pG->grproperty[cur->property].m;
      fb.x1 = m * vd1 * ts1h;
      fb.x2 = m * vd2 * ts1h;
      fb.x3 = m * vd3 * ts1h;

      /* distribute the drag force (density) to the grid */
      distrFB(pG, weight, is, js, ks, fb);
    }
  }/* end of the for loop */

  return;
}

/* Calculate the feedback of the drag force from the particle to the gas
   Serves for the corrector step. It deals with one particle at a time.
   Input: pG: grid with particles
   Output: pG: the array of drag forces exerted by the particle is updated
*/
void feedback_corrector(Grid *pG, Grain *gri, Grain *grf, Vector cell1, Real dv1, Real dv2, Real dv3)
{
  int is, js, ks;
  Real x1, x2, x3, v1, v2, v3;
  Real mgr;
  Real weight[3][3][3];

  Vector fb;

  mgr = pG->grproperty[gri->property].m;
  x1 = 0.5*(gri->x1+grf->x1);
  x2 = 0.5*(gri->x2+grf->x2);
  x3 = 0.5*(gri->x3+grf->x3);
  v1 = 0.5*(gri->v1+grf->v1);
  v2 = 0.5*(gri->v2+grf->v2);
  v3 = 0.5*(gri->v3+grf->v3);

  /* Force other than drag force */
  fb = Get_Force(pG, x1, x2, x3, v1, v2, v3);

  fb.x1 = dv1 - pG->dt*fb.x1;
  fb.x2 = dv2 - pG->dt*fb.x2;
  fb.x3 = dv3 - pG->dt*fb.x3;

  /* Drag force density */
  fb.x1 = mgr*fb.x1;
  fb.x2 = mgr*fb.x2;
  fb.x3 = mgr*fb.x3;

  /* distribute the drag force (density) to the grid */
  getweight(pG, x1, x2, x3, cell1, weight, &is, &js, &ks);
  distrFB(pG, weight, is, js, ks, fb);

#ifdef SHEARING_BOX
#ifndef FARGO
  if (pG->Nx3 > 1) /* 3D shearing box */
  {
    distrFB_shear(pG, weight, is, js, ks, fb);
  }
#endif
#endif /* SHEARING_BOX */

  return;

}

#endif /* FEEDBACK */

/*----------------------------------------------------------------------------*/
/*=========================== PRIVATE FUNCTIONS ==============================*/
/*----------------------------------------------------------------------------*/

/* Delete ghost particles */
void Delete_Ghost(Grid *pG)
{
  long p;
  Grain *gr;

  p = 0;
  while (p<pG->nparticle)
  {/* loop over all particles */
    gr = &(pG->particle[p]);

    if (gr->pos == 0)
    {/* gr is a ghost particle */
      pG->nparticle -= 1;
      pG->grproperty[gr->property].num -= 1;
      pG->particle[p] = pG->particle[pG->nparticle];
    }
    else
      p++;
  }

  return;
}

/* Calcualte the drag force to the particles 
   Input:
     pG: grid;	type: particle type;	cell1: 1/dx1,1/dx2,1/dx3;
     x1,x2,x3,v1,v2,v3: particle position and velocity;
   Output:
     tstop1: 1/stopping time;
   Return:
     drag force;
*/
Vector Get_Drag(Grid *pG, int type, Real x1, Real x2, Real x3, Real v1, Real v2, Real v3, Vector cell1, Real *tstop1)
{
  int is, js, ks;
  Real rho, u1, u2, u3, cs;
  Real vd1, vd2, vd3, vd, tstop, ts1;
  Real weight[3][3][3];		/* weight function */
  Vector fd;

  /* interpolation to get fluid density, velocity and the sound speed */
  getweight(pG, x1, x2, x3, cell1, weight, &is, &js, &ks);

  if (getvalues(pG, weight, is, js, ks, &rho, &u1, &u2, &u3, &cs) == 0)
  { /* particle in the grid */

    /* apply gas velocity shift due to pressure gradient */
    gasvshift(x1, x2, x3, &u1, &u2, &u3);

    /* velocity difference */
    vd1 = v1-u1;
    vd2 = v2-u2;
    vd3 = v3-u3;
    vd = sqrt(SQR(vd1) + SQR(vd2) + SQR(vd3)); /* dimension independent */

    /* particle stopping time */
    tstop = get_ts(pG, type, rho, cs, vd);
    ts1 = 1.0/tstop;
  }
  else
  { /* particle out of the grid, free motion, with warning sign */
    vd1 = 0.0;	vd2 = 0.0;	vd3 = 0.0;	ts1 = 0.0;
    ath_perr(1, "Particle move out of grid %d!\n", pG->my_id); /* level = ? */
  }

  *tstop1 = ts1;

  /* Drag force */
  fd.x1 = -ts1*vd1;
  fd.x2 = -ts1*vd2;
  fd.x3 = -ts1*vd3;

  return fd;
}


/* Calculate the forces to the particle other than the gas drag
   Input:
     pG: grid;
     x1,x2,x3,v1,v2,v3: particle position and velocity;
   Return:
     forces;
*/
Vector Get_Force(Grid *pG, Real x1, Real x2, Real x3, Real v1, Real v2, Real v3)
{
  Vector ft;
  ft.x1 = ft.x2 = ft.x3 = 0.0;

#ifdef SHEARING_BOX
  Real omg2 = SQR(Omega);

  if (pG->Nx3 > 1)
  {/* 3D shearing sheet (x1,x2,x3)=(X,Y,Z) */
  #ifdef FARGO
    ft.x1 += 2.0*v2*Omega;
    ft.x2 += -0.5*v1*Omega;
  #else
    ft.x1 += 3.0*omg2*x1 + 2.0*v2*Omega;
    ft.x2 += -2.0*v1*Omega;
  #endif /* FARGO */
  #ifdef VERTICAL_GRAVITY
    ft.x3 += -omg2*x3;
  #endif /* VERTICAL_GRAVITY */
  }
  else
  { /* 2D shearing sheet (x1,x2,x3)=(X,Z,Y) */
    ft.x1 += 3.0*omg2*x1 + 2.0*v3*Omega;
    ft.x3 += -2.0*v1*Omega;
  #ifdef VERTICAL_GRAVITY
    ft.x2 += -omg2*x2;
  #endif /* VERTICAL_GRAVITY */
  }
#endif /* SHEARING_BOX */

  return ft;
}

#endif /*PARTICLES*/