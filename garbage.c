#include "garbage.h"
#include "allvars.h"
#include "timestep.h"
#include "system.h"
#include "endrun.h"
#include "forcetree.h"

static int
domain_all_garbage_collection();

static int
domain_garbage_collection_slots(int ptype,
    void * storage, int elsize, int * N_slots, int MaxSlots);

int domain_fork_particle(int parent) {
    /* this will fork a zero mass particle at the given location of parent.
     *
     * Assumes the particle is protected by locks in threaded env.
     *
     * The Generation of parent is incremented.
     * The child carries the incremented generation number.
     * The ID of the child is modified, with the new generation number set
     * at the highest 8 bits.
     *
     * the new particle's index is returned.
     *
     * Its mass and ptype can be then adjusted. (watchout detached BH /SPH
     * data!)
     * Its PIndex still points to the old Pindex!
     * */

    if(NumPart >= All.MaxPart)
    {
        endrun(8888,
                "On Task=%d with NumPart=%d we try to spawn. Sorry, no space left...(All.MaxPart=%d)\n",
                ThisTask, NumPart, All.MaxPart);
    }
    /*This is all racy if ActiveParticle or P is accessed from another thread*/
    int child = atomic_fetch_and_add(&NumPart, 1);
    /*Update the active particle list:
     * if the parent is active the child should also be active.
     * Stars must always be active on formation, but
     * BHs need not be: a halo can be seeded when the particle in question is inactive.*/
    if(is_timebin_active(P[parent].TimeBin, All.Ti_Current)) {
        int childactive = atomic_fetch_and_add(&NumActiveParticle, 1);
        ActiveParticle[childactive] = child;
    }

    P[parent].Generation ++;
    uint64_t g = P[parent].Generation;
    /* change the child ID according to the generation. */
    P[child] = P[parent];
    P[child].ID = (P[parent].ID & 0x00ffffffffffffffL) + (g << 56L);

    /* the PIndex still points to the old PIndex */
    P[child].Mass = 0;

    /*! When a new additional star particle is created, we can put it into the
     *  tree at the position of the spawning gas particle. This is possible
     *  because the Nextnode[] array essentially describes the full tree walk as a
     *  link list. Multipole moments of tree nodes need not be changed.
     */

    /* we do this only if there is an active force tree 
     * checking Nextnode is not the best way of doing so though.
     * */
    if(force_tree_allocated()) {
        int no;
        no = Nextnode[parent];
        Nextnode[parent] = child;
        Nextnode[child] = no;
        Father[child] = Father[parent];
    }
    return child;
}

/* remove garbage particles, holes in sph chunk and holes in bh buffer. */
int
domain_garbage_collection(void)
{
    if (force_tree_allocated()) {
        endrun(0, "GC breaks ForceTree invariance. ForceTree must be freed before calling GC.\n");
    }
    /* tree is invalidated if the sequence on P is reordered; */

    int tree_invalid = 0;

    /* TODO: in principle we can track this change and modify the tree nodes;
     * But doing so requires cleaning up the TimeBin link lists, and the tree
     * link lists first. likely worth it, since GC happens only in domain decompose
     * and snapshot IO, both take far more time than rebuilding the tree. */
    tree_invalid |= domain_all_garbage_collection();
    tree_invalid |= domain_garbage_collection_slots(5, BhP, sizeof(BhP[0]), &N_bh_slots, All.MaxPartBh);
    tree_invalid |= domain_garbage_collection_slots(4, StarP, sizeof(StarP[0]), &N_star_slots, All.MaxPartBh);
    tree_invalid |= domain_garbage_collection_slots(0, SphP, sizeof(SphP[0]), &N_sph_slots, All.MaxPart);

    MPI_Allreduce(MPI_IN_PLACE, &tree_invalid, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);

    return tree_invalid;
}

static int
domain_all_garbage_collection()
{
    int i, tree_invalid = 0; 
    int count_elim;
    int64_t total0, total;

    sumup_large_ints(1, &NumPart, &total0);

    count_elim = 0;

    for(i = 0; i < NumPart; i++)
        if(P[i].IsGarbage)
        {
            P[i] = P[NumPart - 1];

            NumPart--;
            i--;

            count_elim++;
        }

    sumup_large_ints(1, &NumPart, &total);

    if(total != total0) {
        message(0, "GC : Reducing Particle slots from %ld to %ld\n", total0, total);
        tree_invalid = 1;
    }
    return tree_invalid;
}

static int slot_cmp_reverse_link(const void * b1in, const void * b2in) {
    const struct particle_data_ext * b1 = (struct particle_data_ext *) b1in;
    const struct particle_data_ext * b2 = (struct particle_data_ext *) b2in;
    if(b1->ReverseLink == -1 && b2->ReverseLink == -1) {
        return 0;
    }
    if(b1->ReverseLink == -1) return 1;
    if(b2->ReverseLink == -1) return -1;
    return (b1->ReverseLink > b2->ReverseLink) - (b1->ReverseLink < b2->ReverseLink);

}

static int
domain_garbage_collection_slots(int ptype,
    void * storage, int elsize, int * N_slots, int MaxSlots)
{
#define SLOT(i) ((struct particle_data_ext *) (((char*) storage) + elsize * (i)))
    /*
     *  BhP is a lifted structure. 
     *  changing BH slots doesn't affect tree's consistency;
     *  this function always return 0. */

    /* gc the bh */
    int i, j;
    int64_t total = 0;

    int64_t total0 = 0;

    sumup_large_ints(1, N_slots, &total0);

    /* If there are no blackholes, there cannot be any garbage. bail. */
    if(total0 == 0) return 0;

#pragma omp parallel for
    for(i = 0; i < MaxSlots; i++) {
        SLOT(i)->ReverseLink = -1;
    }

#pragma omp parallel for
    for(i = 0; i < NumPart; i++) {
        if(P[i].Type == ptype) {
            SLOT(P[i].PI)->ReverseLink = i;
            if(P[i].PI >= *N_slots) {
                endrun(1, "slot PI consistency failed2, N_slots = %d, PI=%d\n", *N_slots, P[i].PI);
            }
            if(SLOT(P[i].PI)->ID != P[i].ID) {
                endrun(1, "slot id consistency failed1\n");
            }
        }
    }

    /* put unused guys to the end, and sort the used ones
     * by their location in the P array */
    qsort(storage, *N_slots, elsize, slot_cmp_reverse_link);

    while(*N_slots > 0 && SLOT(*N_slots - 1)->ReverseLink == -1) {
        (*N_slots) --;
    }
    /* Now update the link in BhP */
    for(i = 0; i < *N_slots; i ++) {
        P[SLOT(i)->ReverseLink].PI = i;
    }

    /* Now invalidate ReverseLink */
    for(i = 0; i < *N_slots; i ++) {
        SLOT(i)->ReverseLink = -1;
    }

    j = 0;
#pragma omp parallel for
    for(i = 0; i < NumPart; i++) {
        if(P[i].Type != ptype) continue;
        if(P[i].PI >= *N_slots) {
            endrun(1, "slot PI consistency failed2\n");
        }
        if(SLOT(P[i].PI)->ID != P[i].ID) {
            endrun(1, "slot id consistency failed2\n");
        }
#pragma omp atomic
        j ++;
    }
    if(j != *N_slots) {
        endrun(1, "slot count failed2, j=%d, N_bh=%d\n", j, *N_slots);
    }

    sumup_large_ints(1, N_slots, &total);

    if(total != total0) {
        message(0, "GC: Reducing number of %d slots from %ld to %ld\n", ptype, total0, total);
    }
    /* slot gc never invalidates the tree */
    return 0;
}



