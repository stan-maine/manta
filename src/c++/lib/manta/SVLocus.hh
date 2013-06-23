// -*- mode: c++; indent-tabs-mode: nil; -*-
//
// Manta
// Copyright (c) 2013 Illumina, Inc.
//
// This software is provided under the terms and conditions of the
// Illumina Open Source Software License 1.
//
// You should have received a copy of the Illumina Open Source
// Software License 1 along with this program. If not, see
// <https://github.com/downloads/sequencing/licenses/>.
//

///
/// \author Chris Saunders
///

#pragma once

#include "blt_util/observer.hh"
#include "blt_util/pos_range.hh"

#include "boost/foreach.hpp"
#include "boost/serialization/map.hpp"
#include "boost/serialization/vector.hpp"
#include "boost/serialization/split_member.hpp"

#include <iostream>
#include <limits>
#include <map>
#include <vector>


//#define DEBUG_SVL


#ifdef DEBUG_SVL
#include "blt_util/log.hh"

#include <iostream>
#endif




// all internal locations use a chromosome index number
struct GenomeInterval
{
    GenomeInterval(
        const int32_t initTid = 0,
        const pos_t beginPos = 0,
        const pos_t endPos = 0) :
        tid(initTid),
        range(beginPos,endPos)
    {}

    /// does this intersect a second GenomeInterval?
    bool
    isIntersect(const GenomeInterval& gi) const
    {
        if (tid != gi.tid) return false;
        return range.is_range_intersect(gi.range);
    }

    bool
    operator<(const GenomeInterval& rhs) const
    {
        if (tid<rhs.tid) return true;
        if (tid == rhs.tid)
        {
            return (range<rhs.range);
        }
        return false;
    }

    bool
    operator==(const GenomeInterval& rhs) const
    {
        return ((tid==rhs.tid) && (range==rhs.range));
    }

    template<class Archive>
    void serialize(Archive & ar, const unsigned /* version */)
    {
        ar & tid & range;
    }

    int32_t tid;
    known_pos_range range;
};


std::ostream&
operator<<(std::ostream& os, const GenomeInterval& gi);

BOOST_CLASS_IMPLEMENTATION(GenomeInterval, boost::serialization::object_serializable)



struct SVLocusEdge
{
    SVLocusEdge(const unsigned init_count = 0) :
        count(init_count)
    {}

    // merge edge into this one
    //
    void
    mergeEdge(SVLocusEdge& edge)
    {
        count += edge.count;
    }

    template<class Archive>
    void serialize(Archive & ar, const unsigned /* version */)
    {
        ar & count;
    }

    unsigned short count;
};


std::ostream&
operator<<(std::ostream& os, const SVLocusEdge& edge);

BOOST_CLASS_IMPLEMENTATION(SVLocusEdge, boost::serialization::object_serializable)



typedef unsigned NodeIndexType;



struct SVLocusNode
{
    typedef std::map<NodeIndexType,SVLocusEdge> edges_type;
    typedef edges_type::iterator iterator;
    typedef edges_type::const_iterator const_iterator;

    SVLocusNode() :
        count(0)
    {}

    // specialized copy ctor which offsets all address:
    SVLocusNode(
            const SVLocusNode& in,
            const unsigned offset) :
        count(in.count),
        interval(in.interval)
    {
        BOOST_FOREACH(const edges_type::value_type& val, in)
        {
            edges.insert(std::make_pair(val.first+offset,val.second));
        }
    }

    unsigned
    size() const
    {
        return edges.size();
    }

    iterator
    begin()
    {
        return edges.begin();
    }

    iterator
    end()
    {
        return edges.end();
    }

    const_iterator
    begin() const
    {
        return edges.begin();
    }

    const_iterator
    end() const
    {
        return edges.end();
    }

    template<class Archive>
    void serialize(Archive & ar,const unsigned /* version */)
    {
        ar & count & interval & edges;
    }

    friend std::ostream&
    operator<<(std::ostream& os, const SVLocusNode& node);

    //////////////////  data:
    unsigned short count;
    GenomeInterval interval;

    edges_type edges;
};


std::ostream&
operator<<(std::ostream& os, const SVLocusNode& node);

BOOST_CLASS_IMPLEMENTATION(SVLocusNode, boost::serialization::object_serializable)



typedef unsigned LocusIndexType;
typedef std::pair<bool, std::pair<LocusIndexType,NodeIndexType> > SVLocusNodeMoveMessage;




/// \brief a set of regions containing dependent SV evidence
///
/// An SV locus is a region hypothetically containing the breakends of 1 to many
/// SVs.
///
/// The locus is composed of a set of non-overlapping contiguous genomic regions and links between them.
/// Each link has an associated evidence count.
///
/// This class internally manages the node shared pointers in a synced data structure, there's probably a better
/// way to do this with transform_iterator, but I've always regretted using that.
///
struct SVLocus : public notifier<SVLocusNodeMoveMessage>
{
    typedef std::vector<SVLocusNode> graph_type;

    typedef graph_type::iterator iterator;
    typedef graph_type::const_iterator const_iterator;

    typedef SVLocusNode::edges_type edges_type;

    SVLocus(const LocusIndexType index = 0) :
        _index(index)
    {}

    bool
    empty() const
    {
        return _graph.empty();
    }

    unsigned
    size() const
    {
        return _graph.size();
    }

    iterator
    begin()
    {
        return _graph.begin();
    }

    iterator
    end()
    {
        return _graph.end();
    }

    const_iterator
    begin() const
    {
        return _graph.begin();
    }

    const_iterator
    end() const
    {
        return _graph.end();
    }

    void
    updateIndex(const LocusIndexType& index)
    {
        _index=index;
    }

    SVLocusNode&
    getNode(const NodeIndexType nodePtr)
    {
        assert(nodePtr<_graph.size());
        return _graph[nodePtr];
    }

    const SVLocusNode&
    getNode(const NodeIndexType nodePtr) const
    {
        assert(nodePtr<_graph.size());
        return _graph[nodePtr];
    }

    NodeIndexType
    addNode(
        const int32_t tid,
        const int32_t beginPos,
        const int32_t endPos)
    {
        NodeIndexType nodePtr(newGraphNode());
        SVLocusNode& node(getNode(nodePtr));
        node.interval.tid=tid;
        node.interval.range.set_range(beginPos,endPos);
        node.count+=1;
        notifyAdd(nodePtr);
        return nodePtr;
    }

    void
    linkNodes(
            const NodeIndexType nodePtr1,
            const NodeIndexType nodePtr2)
    {
        SVLocusNode& node1(getNode(nodePtr1));
        SVLocusNode& node2(getNode(nodePtr2));
        assert(0 == node1.edges.count(nodePtr2));
        assert(0 == node2.edges.count(nodePtr1));
        node1.edges.insert(std::make_pair(nodePtr2,SVLocusEdge(1)));
        node2.edges.insert(std::make_pair(nodePtr1,SVLocusEdge(1)));
    }

    void
    clearNodeEdges(const NodeIndexType nodePtr);

    // copy fromLocus into this locus (this should be an intermediate part of a locus merge)
    void
    copyLocus(const SVLocus& fromLocus)
    {
        assert(&fromLocus != this);

        // simple method -- copy everything in with an offset in all index numbers:
        const unsigned offset(_graph.size());
        BOOST_FOREACH(const SVLocusNode& fromNode, fromLocus)
        {
            const NodeIndexType nodeIndex(newGraphNode());
            getNode(nodeIndex) = SVLocusNode(fromNode, offset);
            notifyAdd(nodeIndex);
        }
    }

    /// join from node into to node
    ///
    /// from node is effectively destroyed,
    //// because all incoming edges will be updated
    ///
    void
    mergeNode(
            const NodeIndexType fromIndex,
            const NodeIndexType toIndex);

    // remove node
    //
    void
    eraseNode(const NodeIndexType nodePtr);

    void
    clear()
    {
        for(NodeIndexType i(0);i<size();++i) notifyDelete(i);
        _graph.clear();
    }

    /// debug func to check that internal data-structures are in
    /// a consistent state
    void
    checkState() const;

    template<class Archive>
    void save(Archive & ar, const unsigned /* version */) const
    {
        ar << _graph;
    }

    template<class Archive>
    void load(Archive & ar, const unsigned /* version */)
    {
        clear();
        ar >> _graph;
    }

    BOOST_SERIALIZATION_SPLIT_MEMBER()

private:

    NodeIndexType
    newGraphNode()
    {
        static const unsigned maxIndex(std::numeric_limits<NodeIndexType>::max());
        unsigned index(_graph.size());
        assert(index<maxIndex);
        _graph.resize(index+1);
        return static_cast<NodeIndexType>(index);
    }

    void
    notifyAdd(const NodeIndexType nodePtr)
    {
#ifdef DEBUG_SVL
            log_os << "SVLocusNotifier: Add node: " << _index << ":" << nodePtr << "\n";
#endif
        notify_observers(std::make_pair(true,std::make_pair(_index,nodePtr)));
    }

    void
    notifyDelete(const NodeIndexType nodePtr)
    {
#ifdef DEBUG_SVL
            log_os << "SVLocusNotifier: Delete node: " << _index << ":" << nodePtr << "\n";
#endif
        notify_observers(std::make_pair(false,std::make_pair(_index,nodePtr)));
    }

    graph_type _graph;
    LocusIndexType _index;
};


std::ostream&
operator<<(std::ostream& os, const SVLocus& locus);

BOOST_CLASS_IMPLEMENTATION(SVLocus, boost::serialization::object_serializable)
