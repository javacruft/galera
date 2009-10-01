/*!
 * @file Group view class (used in the ProtoUpMeta (protolay.hpp)
 */

#ifndef _GCOMM_VIEW_HPP_
#define _GCOMM_VIEW_HPP_

#include <map>

#include <gcomm/common.hpp>
#include <gcomm/uuid.hpp>
#include <gcomm/logger.hpp>

BEGIN_GCOMM_NAMESPACE

class ViewId
{
    UUID    uuid; // uniquely identifies the sequence of group views (?)
    seqno_t seq;  // position in the sequence                        (?)

public:

    ViewId () : uuid(), seq(0) {}

    ViewId (const UUID& uuid_, const seqno_t seq_) :
        uuid  (uuid_),
        seq (seq_)
    {}

    const UUID& get_uuid()  const { return uuid; }
    
    seqno_t     get_seq() const { return seq; }
    
    size_t read (const byte_t* buf, const size_t buflen, const size_t offset)
        throw (gu::Exception);

    size_t write(byte_t* buf, const size_t buflen, const size_t offset) const
        throw (gu::Exception);

    static size_t size() 
    {
        return UUID::size() + sizeof(reinterpret_cast<ViewId*>(0)->seq);
    }

    bool operator<(const ViewId& cmp) const
    {
        return uuid < cmp.uuid || (uuid == cmp.uuid && seq < cmp.seq);
    }

    bool operator==(const ViewId& cmp) const
    {
        return uuid == cmp.uuid && seq == cmp.seq;
    }

    bool operator!=(const ViewId& cmp) const
    {
        return !(*this == cmp);
    }

    std::string to_string() const;
};

// why don't we just inherit from std::map ?
class NodeList 
{
public:

    typedef std::map<const UUID, std::string> Map;
    typedef Map::const_iterator               const_iterator;
    typedef Map::iterator                     iterator;

private:

    Map nodes;

public:

    NodeList() : nodes() {}

    ~NodeList() {}
    
    const_iterator begin() const
    {
        return nodes.begin();
    }

    const_iterator end() const
    {
        return nodes.end();
    }
    
    const_iterator find(const UUID& uuid) const
    {
        return nodes.find(uuid);
    }

    std::pair<iterator, bool> insert(const std::pair<const UUID, const std::string>& p)
    {
        return nodes.insert(p);
    }

    bool empty() const
    {
        return nodes.empty();
    }

    bool operator==(const NodeList& other) const
    {
        return nodes == other.nodes;
    }

    size_t length() const { return nodes.size(); }
    
    static const size_t node_name_size = 16;

    size_t read  (const byte_t*, size_t, size_t) throw (gu::Exception);
    size_t write (byte_t*, size_t, size_t) const throw (gu::Exception);
    size_t size  () const
    {
        return 4 + nodes.size()*(UUID::size() + node_name_size);
    }
};

static inline const UUID& get_uuid(const NodeList::const_iterator i)
{
    return i->first;
}

static inline const std::string& get_name(const NodeList::const_iterator i)
{
    return i->second;
}


class View
{    
public:

    typedef enum
    {
        V_NONE,
        V_TRANS,
        V_REG,
        V_NON_PRIM,
        V_PRIM
    } Type;

    std::string to_string (const Type) const;

private:

    Type     type;
    ViewId   view_id;
    
    NodeList members;
    NodeList joined;
    NodeList left;
    NodeList partitioned;
    
    /* Map pid to human readable string */
    std::string pid_to_string(const UUID& pid) const;

public:

    View() :
        type        (V_NONE),
        view_id     (),
        members     (),
        joined      (),
        left        (),
        partitioned ()
    {}
    
    View(const Type type_, const ViewId& view_id_) : 
        type        (type_),
        view_id     (view_id_),
        members     (),
        joined      (),
        left        (),
        partitioned ()
    {}
    
    ~View() {}
    
    void add_member  (const UUID& pid, const std::string& name = "");

    void add_members (NodeList::const_iterator begin,
                      NodeList::const_iterator end);

    void add_joined      (const UUID& pid, const std::string& name);
    void add_left        (const UUID& pid, const std::string& name);
    void add_partitioned (const UUID& pid, const std::string& name);

    const NodeList& get_members     () const;
    const NodeList& get_joined      () const;
    const NodeList& get_left        () const;
    const NodeList& get_partitioned () const;

    Type          get_type           () const;
    const ViewId& get_id             () const;
    const UUID&   get_representative () const;

    bool is_empty() const;

    size_t read(const byte_t* buf, const size_t buflen, const size_t offset)
        throw (gu::Exception);

    size_t write(byte_t* buf, const size_t buflen, const size_t offset) const
        throw (gu::Exception);

    size_t size() const;
    std::string to_string() const;
};

bool operator==(const View&, const View&);

END_GCOMM_NAMESPACE

#endif // _GCOMM_VIEW_HPP_
