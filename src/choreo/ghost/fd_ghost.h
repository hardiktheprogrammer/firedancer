#ifndef HEADER_fd_src_choreo_ghost_fd_ghost_h
#define HEADER_fd_src_choreo_ghost_fd_ghost_h

/* fd_ghost implements Solana's LMD-GHOST ("latest message-driven greedy
   heaviest-observed subtree") fork choice rule.

   Protocol details:

   - LMD is an acronym for "latest message-driven". It denotes the
     specific flavor of GHOST implementation, ie. only a
     validator's latest vote counts.

   - GHOST is an acronym for "greedy heaviest-observed subtree":
     - greedy:   pick the locally optimal subtree / fork based on our
                 current view (which may not be globally optimal).
     - heaviest: pick based on the highest stake weight.
     - observed: this is the validator's local view, and other
                 validators may have differing views.
     - subtree:  pick a subtree, not an individual node.

   In-memory representation:

   - fd_ghost_node_t implements a left-child right-sibling n-ary tree.
     Each node holds a pointer to its left-most child (`child`), and a
     pointer to its right sibling (`sibling`).

   - Each tree node is keyed by slot number.

   - Each tree node tracks the amount of stake (`stake`) that has voted
     for its slot, as well as the recursive sum of stake for the subtree
     rooted at that node (`weight`).

   - fd_ghost_t is the top-level structure that holds the root of the
     tree, as well as the memory pools and map structures for nodes and
     votes.

   - fd_ghost_vote_t represents a validator's vote.  This includes a
     slot hash and the validator's pubkey and stake.

   Link to original GHOST paper: https://eprint.iacr.org/2013/881.pdf.
   This is simply a reference for those curious about the etymology, and
   not prerequisite reading for understanding this implementation. */

#include "../fd_choreo_base.h"

/* FD_GHOST_USE_HANDHOLDING:  Define this to non-zero at compile time
   to turn on additional runtime checks and logging. */

#ifndef FD_GHOST_USE_HANDHOLDING
#define FD_GHOST_USE_HANDHOLDING 1
#endif

#define FD_GHOST_PRINT_DEPTH_DEFAULT 8

/* clang-format off */
typedef struct fd_ghost_node fd_ghost_node_t;
struct __attribute__((aligned(128UL))) fd_ghost_node {
  ulong             slot;         /* both the ghost and map key */
  ulong             next;         /* reserved for internal use by fd_pool and fd_map_chain */
  ulong             weight;       /* amount of stake that has voted for this slot hash or descendants */
  ulong             stake;        /* amount of stake that has voted for this slot hash */
  ulong             gossip_stake; /* amount of stake from gossip votes (sans replay overlap) */
  ulong             rooted_stake; /* amount of stake that has rooted this slot */
  int               eqv;          /* flag for equivocation (multiple blocks) in this slot */
  fd_ghost_node_t * parent;       /* pointer to the parent */
  fd_ghost_node_t * child;        /* pointer to the left-most child */
  fd_ghost_node_t * sibling;      /* pointer to next sibling */
};

#define FD_GHOST_EQV_SAFE ( 0.52 )
#define FD_GHOST_OPT_CONF ( 2.0 / 3.0 )

#define POOL_NAME fd_ghost_node_pool
#define POOL_T    fd_ghost_node_t
#include "../../util/tmpl/fd_pool.c"

#define MAP_NAME               fd_ghost_node_map
#define MAP_ELE_T              fd_ghost_node_t
#define MAP_KEY                slot
#include "../../util/tmpl/fd_map_chain.c"

struct fd_ghost_vote {
  fd_pubkey_t    pubkey;    /* validator identity, also the map key */
  ulong          next;      /* reserved for internal use by fd_pool and fd_map_chain */
  ulong          slot;      /* slot being voted for */
  ulong          stake;     /* validator's stake */
};
typedef struct fd_ghost_vote fd_ghost_vote_t;

#define POOL_NAME fd_ghost_vote_pool
#define POOL_T    fd_ghost_vote_t
#include "../../util/tmpl/fd_pool.c"

#define MAP_NAME               fd_ghost_vote_map
#define MAP_ELE_T              fd_ghost_vote_t
#define MAP_KEY                pubkey
#define MAP_KEY_T              fd_pubkey_t
#define MAP_KEY_EQ(k0,k1)      (!(memcmp((k0)->hash,(k1)->hash,sizeof(fd_hash_t))))
#define MAP_KEY_HASH(key,seed) (key->ui[0]^seed)
#include "../../util/tmpl/fd_map_chain.c"

struct __attribute__((aligned(128UL))) fd_ghost {
  fd_ghost_node_t *     root;
  fd_ghost_node_t *     node_pool; /* memory pool of ghost nodes */
  fd_ghost_node_map_t * node_map;  /* map of slot_hash->fd_ghost_node_t */
  fd_ghost_vote_t *     vote_pool; /* memory pool of ghost votes */
  fd_ghost_vote_map_t * vote_map;  /* each node's latest vote. map of pubkey->fd_ghost_vote_t */
};
typedef struct fd_ghost fd_ghost_t;

/* clang-format on */

FD_PROTOTYPES_BEGIN

/* fd_ghost_{align,footprint} return the required alignment and
   footprint of a memory region suitable for use as ghost with up to
   node_max nodes and vote_max votes. */

FD_FN_CONST static inline ulong
fd_ghost_align( void ) {
  return alignof( fd_ghost_t );
}

/* clang-format off */
FD_FN_CONST static inline ulong
fd_ghost_footprint( ulong node_max, ulong vote_max ) {
  return FD_LAYOUT_FINI(
    FD_LAYOUT_APPEND(
    FD_LAYOUT_APPEND(
    FD_LAYOUT_APPEND(
    FD_LAYOUT_APPEND(
    FD_LAYOUT_APPEND(
    FD_LAYOUT_INIT,
      alignof( fd_ghost_t ),      sizeof( fd_ghost_t ) ),
      fd_ghost_node_pool_align(), fd_ghost_node_pool_footprint( node_max ) ),
      fd_ghost_node_map_align(),  fd_ghost_node_map_footprint( node_max ) ),
      fd_ghost_vote_pool_align(), fd_ghost_vote_pool_footprint( vote_max ) ),
      fd_ghost_vote_map_align(),  fd_ghost_vote_map_footprint( vote_max ) ),
    alignof( fd_ghost_t ) );
}
/* clang-format on */

/* fd_ghost_new formats an unused memory region for use as a ghost.
   mem is a non-NULL pointer to this region in the local address space
   with the required footprint and alignment. */

void *
fd_ghost_new( void * mem, ulong node_max, ulong vote_max, ulong seed );

/* fd_ghost_join joins the caller to the ghost.  ghost points to the
   first byte of the memory region backing the ghost in the caller's
   address space.

   Returns a pointer in the local address space to ghost on success. */

fd_ghost_t *
fd_ghost_join( void * ghost );

/* fd_ghost_leave leaves a current local join.  Returns a pointer to the
   underlying shared memory region on success and NULL on failure (logs
   details).  Reasons for failure include ghost is NULL.
 */

void *
fd_ghost_leave( fd_ghost_t const * ghost );

/* fd_ghost_delete unformats a memory region used as a ghost.
   Assumes only the nobody is joined to the region.  Returns a
   pointer to the underlying shared memory region or NULL if used
   obviously in error (e.g. ghost is obviously not a ghost ... logs
   details).  The ownership of the memory region is transferred to the
   caller. */

void *
fd_ghost_delete( void * ghost );

/* fd_ghost_init initializes a ghost.  Assumes fd_ghost_new and 
   fd_ghost_join and ghost is a valid local join root_slot_hash is the
   initial root ghost will use.  This is the snapshot slot if booting
   with a snapshot, genesis slot if not.

   This should generally not be called by anyone other than the owner of
   ghost's memory (ie. same caller as fd_ghost_new.) */
void
fd_ghost_init( fd_ghost_t * ghost, ulong slot );

/* fd_ghost_head_query returns the head of the ghost.  Ghost is always
   non-empty, ie. a root must exist. */

fd_ghost_node_t *
fd_ghost_head_query( fd_ghost_t * ghost );

/* fd_ghost_leaf_insert inserts a new leaf node with key into the ghost,
   with parent_slot_hash optionally specified.  The caller promises
   slot_hash is not currently in the map and parent_slot_hash is. */

fd_ghost_node_t *
fd_ghost_node_insert( fd_ghost_t * ghost, ulong slot, ulong parent_slot );

/* fd_ghost_node_query queries and returns the node keyed by slot_hash.
   Returns NULL if not found. */

fd_ghost_node_t *
fd_ghost_node_query( fd_ghost_t * ghost, ulong slot );

/* fd_ghost_replay_vote_upsert inserts a replay vote into ghost.

   The stake associated with pubkey is added to the ancestry chain
   beginning at slot hash ("insert").  If pubkey has previously voted,
   the previous vote's stake is removed from the previous vote slot
   hash's ancestry chain ("update").

   TODO the implementation can be made more efficient by
   short-circuiting and doing fewer traversals, but as it exists this is
   bounded to O(h), where h is the height of ghost. */

void
fd_ghost_replay_vote_upsert( fd_ghost_t * ghost, ulong slot, fd_pubkey_t const * pubkey, ulong stake );

/* fd_ghost_gossip_vote_upsert inserts a gossip vote into ghost.

   Unlike fd_ghost_replay_vote_upsert, the stake associated with pubkey
   is not propagated to ancestors of slot_hash.  It is only counted
   towards slot_hash itself. */

void
fd_ghost_gossip_vote_upsert( fd_ghost_t * ghost, ulong slot, fd_pubkey_t const * pubkey, ulong stake );

/* fd_ghost_publish publishes the subtree at root as the new ghost tree,
   pruning all nodes prior to root. */
void
fd_ghost_publish( fd_ghost_t * ghost, fd_ghost_node_t * root );

/* fd_ghost_print pretty-prints a formatted ghost tree.  start controls
   which node to begin printing from.  depth controls how many
   additional ancestors to walk back from start to begin printing from.
   total_stake controls how much total stake is active to enable
   percentage calculations.

   FD_SLOT_NULL, 0 and 0 are valid defaults for the above, respectively.
   In that case, ghost would begin printing from the root without
   percentages (not recommended because the output will be difficult
   to read).
   
   Typical usage is to pass in the most recently executed slot hash for
   start, so that start is always in a leaf position, and pick an
   appropriate depth for visualization (FD_GHOST_PRINT_DEPTH_DEFAULT
   is the recommended default). */

void
fd_ghost_print( fd_ghost_t * ghost, ulong start, ulong depth, ulong total_stake );

FD_PROTOTYPES_END

#endif /* HEADER_fd_src_choreo_ghost_fd_ghost_h */
