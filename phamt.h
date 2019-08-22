/**
 *  \file phamt.h
 *  \author Ryan C. Gordon.
 *  \brief Templated Persistent Hash-Array-Mapped Trie 
 */

#ifndef _INCL_PHAMT_H_
#define _INCL_PHAMT_H_

#include <assert.h>

#define Assert assert

template<class MapFrom>
uint32 hashCalculate(const MapFrom &from);

template<class MapFrom>
bool hashFromMatch(const MapFrom &obj1, const MapFrom &obj2);



// https://www.youtube.com/watch?v=WT9kmIE3Uis

template <class MapFrom, class MapTo>
class PersistentTrie
{
public:
    PersistentTrie()
        : generation(0)
        , root(new BranchNode(0))
        , numbranches(1)
        , numleaves(0)
        , numbuckets(0)
    {}

    ~PersistentTrie()
    {
        root->unref();
    }

    // Copy construction just does a shallow copy, and increments the root refcount and generation.
    //  Both the original and copy can then modify the tree without stepping on each other.
    PersistentTrie(const PersistentTrie &m)
        : generation(m.generation + 1)
        , root(m.root)
        , numbranches(m.numbranches)
        , numleaves(m.numleaves)
        , numbuckets(m.numbuckets)
    {
        root->addref();
    }

    bool isEmpty() const { return numleaves == 0; }
    uint32 count() const { return numleaves; }

    // this either inserts or updates
    MapTo &put(const MapFrom &from, const MapTo &to)
    {
        FindLeafData data;
        LeafNode *leaf = findLeaf(from, data);

        if (!leaf)  // doesn't exist, have to create a new leaf.
        {
            leaf = new LeafNode(generation, from, to);
            addToAncestors(leaf, data);
            numleaves++;
        }
        else  // found a leaf for this hash.
        {
            if (hashFromMatch<MapFrom>(from, leaf->from))  // this is the correct leaf; it's an update.
            {
                // we always replace the leaf here so we don't have one path where we don't use the copy constructor.
                leaf = new LeafNode(generation, from, to);
                replaceAncestors(leaf, data);
            }
            else   // not the correct leaf! Need to split into a new branch!
            {
                LeafNode *leaf1;
                if (leaf->generation != generation)  // from a previous snapshot, so duplicate it.
                    leaf1 = new LeafNode(generation, leaf->from, leaf->to);
                else
                {
                    leaf1 = (LeafNode *) leaf->addref();  // replaceAncestors will unref this in a moment.
                }

                LeafNode *leaf2 = new LeafNode(generation, from, to);
                numleaves++;


                const uint32 oldhash = hashCalculate<MapFrom>(leaf->from) & 0x3fffffff;  // "& 0x3fffffff" == ignore top 2 bits.

                // if both leaves match for more than one extra branch, we might need to build out more tree.
                while (1)
                {
                    const uint32 sparseidx1 = (oldhash >> (data.numAncestors * 5)) & 31;
                    const uint32 sparseidx2 = (data.hash >> (data.numAncestors * 5)) & 31;
                    if (sparseidx1 != sparseidx2)  // okay, no collision, add it all.
                    {
                        BranchNode *branch = new BranchNode(generation, (1 << sparseidx1) | (1 << sparseidx2), new Node*[2]);
                        branch->children[0] = (sparseidx1 < sparseidx2) ? leaf1 : leaf2;
                        branch->children[1] = (sparseidx1 < sparseidx2) ? leaf2 : leaf1;
                        replaceAncestors(branch, data);
                        numbranches++;
                        leaf = leaf2;
                        break;
                    }

                    // collision, need another branch!

                    // if this is a full hash collision, we need a node to act as a bucket so both leaves
                    //  can live at the same hash.
                    else if (data.numAncestors >= 6)
                    {
                        Assert(oldhash == data.hash);
                        Node *parent = data.ancestors[data.numAncestors-1];
                        if (parent->nodetype == NODETYPE_BRANCH)
                        {
                            Assert(data.numAncestors == 6);  // we use 30 bits for the hash.
                            BucketNode *bucket = new BucketNode(generation, new LeafNode*[2], 2);
                            bucket->leaves[0] = leaf1;
                            bucket->leaves[1] = leaf2;
                            replaceAncestors(bucket, data);
                            Assert(data.numAncestors < (sizeof (data.ancestors) / sizeof (data.ancestors[0])));
                            data.ancestors[data.numAncestors++] = bucket;
                            numbuckets++;
                        }
                        else
                        {
                            Assert(parent->nodetype == NODETYPE_BUCKET);
                            Assert(data.numAncestors == 7);  // we use 30 bits for the hash, but treat the bucket as an extra ancestor.

                            BucketNode *bucket = (BucketNode *) parent;
                            const uint32 numleaves = bucket->numleaves;
                            LeafNode **leaves = new LeafNode*[numleaves + 1];
                            for (uint32 i = 0; i < numleaves; i++)
                                leaves[i] = bucket->leaves[i];
                            leaves[numleaves] = leaf2;

                            if (bucket->generation != generation)
                                replaceAncestors(new BucketNode(generation, leaves, numleaves+1), data);
                            else
                            {
                                delete[] bucket->leaves;
                                bucket->leaves = leaves;
                                bucket->numleaves++;
                                if (leaf->generation == generation)
                                    leaf->unref();  // we added a ref up there.  :/
                            }
                        }
                        break;
                    }
                    else
                    {
                        BranchNode *branch = new BranchNode(generation, (1 << sparseidx1), new Node*[1]);
                        branch->children[0] = NULL;  // replaceAncestors will fix this later.
                        replaceAncestors(branch, data);
                        Assert(data.numAncestors < (sizeof (data.ancestors) / sizeof (data.ancestors[0])));
                        data.ancestors[data.numAncestors++] = branch;
                        numbranches++;
                    }
                }
            }
        }

        return leaf->to;
    }

    MapTo *get(const MapFrom &from) const
    {
        FindLeafData data;
        LeafNode *leaf = findLeaf(from, data);
        if (leaf)  // found a leaf for this hash.
        {
            if (hashFromMatch<MapFrom>(from, leaf->from))  // this is the correct leaf; we're done!
                return &leaf->to;
        }
        return NULL;  // We don't have this specific thing.
    }

    MapTo *get(const MapFrom &from, MapTo &deflt) const
    {
        MapTo *to = get(from);
        return to ? to : &deflt;
    }

    void remove(const MapFrom &from)
    {
        FindLeafData data;
        LeafNode *leaf = findLeaf(from, data);
        if (leaf && hashFromMatch<MapFrom>(from, leaf->from))  // found the correct leaf; nuke it.
        {
            removeFromAncestors(leaf, data);
            numleaves--;
        }
    }

    void flush()
    {
        generation++;
        root->unref();
        root = new BranchNode(generation);
        numbranches = 1;
        numleaves = 0;
    }

    PersistentTrie *snapshot()
    {
        generation++;
        return new PersistentTrie(*this);
    }

    typedef void (*IterFunc)(const MapFrom &from, MapTo &to, void *data);

    void iterate(IterFunc iter, void *userdata=NULL)
    {
        iterateBranch(root, iter, userdata);
    }

    // !!! FIXME: cull() not implemented: can't change the tree while walking it, since it might make new revisions of nodes we're walking.

private:
    enum NodeType { NODETYPE_BRANCH, NODETYPE_LEAF, NODETYPE_BUCKET };

    struct Node
    {
        Node *addref() { Assert(refcount > 0); refcount++; return this; }
        void unref() { Assert(refcount > 0); if (--refcount == 0) { delete this; } }

        const NodeType nodetype;
        const uint32 generation;
        uint32 refcount;

    protected:  // construct subclasses instead, please.
        Node(const NodeType _nodetype, const uint32 _gen) : nodetype(_nodetype), generation(_gen), refcount(1) {}

        // !!! FIXME: lose the vtable
        virtual ~Node() {}  // use unref() when you are done. Unref everything and this will delete.
    };

    struct LeafNode : public Node
    {
        LeafNode(const uint32 _gen, const MapFrom &_from, const MapTo &_to)
            : Node(NODETYPE_LEAF, _gen), from(_from), to(_to) {}
        virtual ~LeafNode() {}
        MapFrom from;
        MapTo to;
    };

    struct BranchNode : public Node
    {
        BranchNode(const uint32 _gen, const uint32 _sparsemap=0, Node **_children=NULL)
            : Node(NODETYPE_BRANCH, _gen), sparsemap(_sparsemap), children(_children) {}

        virtual ~BranchNode()
        {
            const int numkids = popcount(sparsemap);
            for (int i = 0; i < numkids; i++) {
                children[i]->unref();
            }
            delete[] children;
        }

        uint32 sparsemap;
        Node **children;
    };

    struct BucketNode : public Node
    {
        BucketNode(const uint32 _gen, LeafNode **_leaves, const uint32 _numleaves)
            : Node(NODETYPE_BUCKET, _gen), leaves(_leaves), numleaves(_numleaves) {}

        virtual ~BucketNode()
        {
            for (uint32 i = 0; i < numleaves; i++) {
                leaves[i]->unref();
            }
            delete[] leaves;
        }

        LeafNode **leaves;
        uint32 numleaves;
    };


    struct FindLeafData
    {
        int numAncestors;
        Node *ancestors[8];
        uint32 hash;
    };

    uint32 generation;
    BranchNode *root;
    uint32 numbranches;
    uint32 numleaves;
    uint32 numbuckets;

    static inline int popcount(const uint32 x)
    {
        // !!! FIXME: on Intel targets, MSVC has __popcnt in <intrin.h>. This is considered an SSE 4.2 instruction (~2008...the start of the "Core" line of CPUs).
        // __builtin_popcount has a software fallback for non-Intel systems and x86 without the opcode.
        #if defined(__GNUC__) || defined(__clang__)
        return __builtin_popcount((unsigned int) x);
        #else
        // Hamming Weight in C: https://stackoverflow.com/a/109025
        x = x - ((x >> 1) & 0x55555555);
        x = (x & 0x33333333) + ((x >> 2) & 0x33333333);
        return (int) ((((x + (x >> 4)) & 0x0F0F0F0F) * 0x01010101) >> 24);
        #endif
    }

    static inline int sparseToCompact(const uint32 sparsemap, const int sparseidx)
    {
        const uint32 mask = (1 << sparseidx) - 1;
        const int retval = popcount(sparsemap & mask);
        Assert(retval >= 0);
        Assert(retval < 32);
        return retval;
    }

    void replaceAncestors(Node *child, FindLeafData &data)
    {
        //printf("REPLACE ANCESTORS\n");
        if (data.numAncestors == 0) {
            Assert(child->nodetype == NODETYPE_BRANCH);
            root->unref();
            root = (BranchNode *) child;
            return;
        }

        Assert(data.ancestors[0] == root);
        Assert(data.numAncestors <= (sizeof (data.ancestors) / sizeof (data.ancestors[0])));
        int start = data.numAncestors-1;

        // might be a bucket right at the start...
        if (data.ancestors[start]->nodetype == NODETYPE_BUCKET)
        {
            Assert(child->nodetype == NODETYPE_LEAF);
            BucketNode *bucket = (BucketNode *) data.ancestors[start];
            LeafNode *leaf = (LeafNode *) child;
            const uint32 numleaves = bucket->numleaves;
            uint32 pos;
            for (pos = 0; pos < numleaves; pos++)
            {
                if (hashFromMatch<MapFrom>(bucket->leaves[pos]->from, leaf->from))
                    break;
            }

            Assert(pos < numleaves);

            bucket->leaves[pos]->unref();

            if (bucket->generation == generation)
                bucket->leaves[pos] = leaf;
            else
            {
Assert(!"write me");  // bucket = new blah(leaves_array_we_copied, blah); ref and unref whatever.
            }

            bucket->addref();  // will unref in a moment.
            child = bucket;
            start--;
        }

        for (int i = start; i >= 0; i--)
        {
            BranchNode *a = (BranchNode *) data.ancestors[i];
            Assert(a->nodetype == NODETYPE_BRANCH);
            const int numkids = popcount(a->sparsemap);
            const bool samegen = (a->generation == generation);

            const uint32 sparseidx = (data.hash >> (i * 5)) & 31;
            Assert(a->sparsemap & (1 << sparseidx));  // has to be a replacement, not an insertion

            const int compactidx = sparseToCompact(a->sparsemap, sparseidx);

            // this assert is no longer valid because we update data.ancestors as we walk back up the tree.
            //Assert((i == start) || (a->children[compactidx] == data.ancestors[i+1]));

            if (samegen)
            {
                if (a->children[compactidx])  // !!! FIXME: is this ever NULL?
                    a->children[compactidx]->unref();
                a->children[compactidx] = child;
                return;   // no need to walk further up the tree.
            }

            // part of a snapshot, duplicate it.
            BranchNode *b = new BranchNode(generation, a->sparsemap, new Node*[numkids]);

            for (int j = 0; j < compactidx; j++)
                b->children[j] = a->children[j]->addref();

            b->children[compactidx] = child;

            for (int j = compactidx+1; j < numkids; j++)
                b->children[j] = a->children[j]->addref();

            //a->unref();  // don't unref here; we'll kill it when we unref it as its parent's child.

            data.ancestors[i] = child = b;
        }

        Assert(child->nodetype == NODETYPE_BRANCH);
        root->unref();
        root = (BranchNode *) child;
    }

    void addToAncestors(LeafNode *child, FindLeafData &data)
    {
        Assert(data.numAncestors > 0);
        const int ancestoridx = data.numAncestors - 1;
        BranchNode *branch = (BranchNode *) data.ancestors[ancestoridx];
        const bool samegen = (branch->generation == generation);

        if (branch->nodetype == NODETYPE_BRANCH)
        {
            const uint32 sparseidx = (data.hash >> (ancestoridx * 5)) & 31;

            Assert((branch->sparsemap & (1 << sparseidx)) == 0);  // has to be an insertion, not a replacement.
            const uint32 sparsemap = branch->sparsemap | (1 << sparseidx);
            const int numkids = popcount(sparsemap);
            Node **origkids = branch->children;
            Node **newkids = new Node*[numkids];

            const int before = sparseidx ? popcount(sparsemap & ((1 << sparseidx) - 1)) : 0;  // num kids before insertion point.

            if (samegen)  // current generation, just update the node.
            {
                for (int i = 0; i < before; i++) newkids[i] = origkids[i];
                newkids[before] = child;
                for (int i = before+1; i < numkids; i++) newkids[i] = origkids[i-1];
                branch->sparsemap = sparsemap;
                branch->children = newkids;
                delete[] origkids;
            }
            else  // part of a snapshot, duplicate it.
            {
                for (int i = 0; i < before; i++) newkids[i] = origkids[i]->addref();
                newkids[before] = child;
                for (int i = before+1; i < numkids; i++) newkids[i] = origkids[i-1]->addref();
                data.numAncestors--;
                replaceAncestors(new BranchNode(generation, sparsemap, newkids), data);
                data.numAncestors++;
            }
        }
        else if (branch->nodetype == NODETYPE_BUCKET)
        {
            BucketNode *bucket = (BucketNode *) branch;
            Assert(child->nodetype == NODETYPE_LEAF);
            const uint32 numleaves = bucket->numleaves;
            LeafNode **leaves = new LeafNode*[numleaves+1];
            if (samegen)  // current generation, just update the node.
            {
                for (uint32 i = 0; i < numleaves; i++)
                {
                    Assert(bucket->leaves[i] != child);
                    leaves[i] = bucket->leaves[i];
                }
                leaves[numleaves] = child;
                delete[] bucket->leaves;
                bucket->leaves = leaves;
                bucket->numleaves++;
            }
            else
            {
                for (uint32 i = 0; i < numleaves; i++)
                {
                    Assert(bucket->leaves[i] != child);
                    leaves[i] = (LeafNode *) bucket->leaves[i]->addref();
                }
                leaves[numleaves] = child;

                data.numAncestors--;
                replaceAncestors(new BucketNode(generation, leaves, bucket->numleaves+1), data);
                data.numAncestors++;
            }
        }
        else
        {
            Assert(!"Unexpected node type");
        }
    }

    void removeFromAncestors(LeafNode *leaf, const FindLeafData &data)
    {
        Assert(data.numAncestors > 0);
        const int ancestoridx = data.numAncestors - 1;
        BranchNode *branch = data.ancestors[ancestoridx];
        const bool samegen = (branch->generation == generation);

        if (branch->nodetype == NODETYPE_BRANCH)
        {
            const uint32 sparseidx = (data.hash >> (ancestoridx * 5)) & 31;
            Assert((branch->sparsemap & (1 << sparseidx)) != 0);  // has to be a removal.
            const uint32 sparsemap = branch->sparsemap & ~(1 << sparseidx);
            const int numkids = popcount(sparsemap);
            Node **origkids = branch->children;
            Node **newkids = new Node*[numkids];

            const int before = sparseidx ? popcount(sparsemap & ((1 << sparseidx) - 1)) : 0;  // num kids before insertion point.
            Assert(origkids[before]->nodetype == NODETYPE_LEAF);

            if (samegen)  // current generation, just update the node.
            {
                for (int i = 0; i < before; i++) newkids[i] = origkids[i];
                for (int i = before; i < numkids; i++) newkids[i] = origkids[i+1];
                branch->sparsemap = sparsemap;
                branch->children = newkids;
                delete[] origkids;
            }
            else  // part of a snapshot, duplicate it.
            {
                for (int i = 0; i < before; i++) newkids[i] = origkids[i]->addref();
                for (int i = before; i < numkids; i++) newkids[i] = origkids[i+1]->addref();
                replaceAncestors(new BranchNode(generation, sparsemap, newkids), data);
            }
        }
        else if (branch->nodetype == NODETYPE_BUCKET)
        {
            BucketNode *bucket = (BucketNode *) branch;
            Assert(leaf->nodetype == NODETYPE_LEAF);
            const uint32 numleaves = bucket->numleaves;
            uint32 pos;
            for (pos = 0; pos < numleaves; pos++)
            {
                if (bucket->leaves[pos] == leaf)
                    break;
            }

            Assert(pos < numleaves);

            if (samegen)  // current generation, just update the node.
            {
                while (pos < numleaves)
                {
                    bucket->leaves[pos] = bucket->leaves[pos+1];
                    pos++;
                }
                bucket->numleaves--;
            }
            else
            {
                LeafNode **leaves = new LeafNode*[numleaves-1];
                for (uint32 i = 0; i < pos; i++) leaves[i] = bucket->leaves[i]->addref();
                for (uint32 i = pos; i < numleaves-1; i++) leaves[i] = bucket->leaves[i+1]->addref();
                data.numAncestors--;
                replaceAncestors(new BucketNode(generation, leaves, numleaves-1), data);
                data.numAncestors++;
            }
        }
        else
        {
            Assert(!"Unexpected node type");
        }

        leaf->unref();
    }

    LeafNode *findLeaf(const MapFrom &from, FindLeafData &data) const
    {
        const uint32 hash = hashCalculate<MapFrom>(from) & 0x3fffffff;  // "& 0x3fffffff" == ignore top 2 bits.
        Node *node = root;

        Assert(node != NULL);

        data.numAncestors = 0;
        data.hash = hash;

        for (uint32 i = 0; i < 30; i += 5)
        {
            Assert(node->nodetype == NODETYPE_BRANCH);
            BranchNode *branch = (BranchNode *) node;
            const uint32 sparseidx = (hash >> i) & 31;
            const uint32 bit = 1 << sparseidx;

            Assert(data.numAncestors < (sizeof (data.ancestors) / sizeof (data.ancestors[0])));
            data.ancestors[data.numAncestors++] = branch;

            if ((branch->sparsemap & bit) == 0)  // no node here, so this leaf doesn't exist yet.
                return NULL;

            else  // there's a node here.
            {
                node = branch->children[sparseToCompact(branch->sparsemap, sparseidx)];

                // Already a leaf here? Either we found it, or it's a conflict.
                switch (node->nodetype)
                {
                    case NODETYPE_BRANCH:
                        break;  // go through the loop again with the deeper branch node.

                    case NODETYPE_LEAF:
                        // Note that this MAY NOT be the leaf you wanted! This is
                        //  just the first leaf that matched the hash. Users of
                        //  this function should call:
                        //   if (hashFromMatch<MapFrom>(from, leaf->from))
                        //  and decide what to do. If writing to the trie, you
                        //  might have a conflict and have to get a new branch
                        //  in there!
                        return (LeafNode *) node;  // This is us; we already exist.

                    case NODETYPE_BUCKET: {  // total hash collision!
                        BucketNode *bucket = (BucketNode *) node;
                        Assert(data.numAncestors < (sizeof (data.ancestors) / sizeof (data.ancestors[0])));
                        data.ancestors[data.numAncestors++] = bucket;
                        for (uint32 j = 0; j < bucket->numleaves; j++)
                        {
                            if (hashFromMatch<MapFrom>(from, bucket->leaves[j]->from))
                                return bucket->leaves[j];
                        }
                        return NULL;   // definitely not in the tree.
                    }

                    default: Assert(!"unexpected node type!"); return NULL;
                }
            }
        }

        Assert(!node);  // Ran out of hash bits before we ran out of branches?!
        return NULL;  // really, you should never hit this.
    }

    void iterateBranch(BranchNode *branch, IterFunc iter, void *userdata)
    {
        Node **_node = branch->children;
        const int numkids = popcount(branch->sparsemap);
        for (int i = 0; i < numkids; i++, _node++)
        {
            Node *node = *_node;
            switch (node->nodetype)
            {
                case NODETYPE_BRANCH:
                    iterateBranch((BranchNode *) node, iter, userdata);
                    break;
                case NODETYPE_LEAF:
                    iter(node->from, node->to, userdata);
                    break;
                case NODETYPE_BUCKET:
                    for (uint32 j = 0; j < node->numleaves; j++)
                        iter(node->leaves[j]->from, node->leaves[j]->to, userdata);
                    break;
            }
        }
    }

    #if 1
    void verifyBranch(BranchNode *branch) const
    {
        Assert(branch->nodetype == NODETYPE_BRANCH);
        Node **_node = branch->children;
        const int numkids = popcount(branch->sparsemap);
        for (int i = 0; i < numkids; i++, _node++)
        {
            Node *node = *_node;
            switch (node->nodetype)
            {
                case NODETYPE_BRANCH:
                    verifyBranch((BranchNode *) node);
                    break;

                case NODETYPE_LEAF:
                {
                    FindLeafData data;
                    LeafNode *leaf = findLeaf(node->from, data);
                    Assert(leaf == node);
                    break;
                }

                case NODETYPE_BUCKET:
                    for (uint32 j = 0; j < node->numleaves; j++)
                    {
                        FindLeafData data;
                        LeafNode *leaf = findLeaf(node->leaves[j]->from, data);
                        Assert(leaf == node->leaves[j]);
                    }
                    break;
            }
        }
    }
    public:
    void verify() const { verifyBranch(root); printf("VERIFY: %d leaves, %d branches, %d buckets\n", (int) numleaves, (int) numbranches, (int) numbuckets); }
    #endif
};

#endif

// end of phamt.h ...

