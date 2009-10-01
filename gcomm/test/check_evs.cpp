
#include "check_gcomm.hpp"

#include "evs_proto.hpp"
#include "evs.hpp"
#include "gcomm/pseudofd.hpp"

#include <vector>
#include <queue>
#include <deque>
#include <algorithm>
#include <stdexcept>
#include <iterator>

#include <check.h>

#include "check_templ.hpp"
#include "combination.hpp"

using std::vector;
using std::deque;

using namespace gcomm;

static const uint32_t EVS_SEQNO_MAX = 0x800U;

struct delete_object
{
    template <typename T>
    void operator()(T* t)
    {
        delete t;
    }
};


START_TEST(test_seqno)
{
    fail_unless(seqno_eq(0, 0));
    fail_unless(seqno_eq(SEQNO_MAX, SEQNO_MAX));
    fail_if(seqno_eq(5, SEQNO_MAX));
    fail_if(seqno_eq(SEQNO_MAX, 7));

    fail_unless(seqno_lt(2, 4));
    fail_unless(seqno_lt(SEQNO_MAX - 5, SEQNO_MAX - 2));
    fail_unless(seqno_lt(SEQNO_MAX - 5, 1));
    fail_if(seqno_lt(5, 5));
    fail_if(seqno_lt(SEQNO_MAX - 5, SEQNO_MAX - 5));
    fail_if(seqno_lt(5, 5 + SEQNO_MAX/2));

    fail_unless(seqno_gt(4, 2));
    fail_unless(seqno_gt(SEQNO_MAX - 2, SEQNO_MAX - 5));
    fail_unless(seqno_gt(1, SEQNO_MAX - 5));
    fail_if(seqno_gt(5, 5));
    fail_if(seqno_gt(SEQNO_MAX - 5, SEQNO_MAX - 5));
    fail_unless(seqno_gt(5, 5 + SEQNO_MAX/2));


    fail_unless(seqno_eq(seqno_add(1, 5), 6));
    fail_unless(seqno_eq(seqno_add(SEQNO_MAX - 5, 6), 1));
    fail_unless(seqno_eq(seqno_add(7, SEQNO_MAX - 5), 2));
    // fail_unless(seqno_eq(seqno_add(7, SEQNO_MAX), 7));
    // fail_unless(seqno_eq(seqno_add(SEQNO_MAX, SEQNO_MAX), 0));

    fail_unless(seqno_eq(seqno_dec(0, 1), SEQNO_MAX - 1));
    fail_unless(seqno_eq(seqno_dec(7, SEQNO_MAX - 5), 12));
    fail_unless(seqno_eq(seqno_dec(42, 5), 37));

    fail_unless(seqno_eq(seqno_next(SEQNO_MAX - 1), 0));
}
END_TEST

BEGIN_GCOMM_NAMESPACE

static bool operator==(const EVSMessage& a, const EVSMessage& b)
{
    return a.get_type()      == b.get_type() &&
        a.get_user_type()     == b.get_user_type() &&
        a.get_safety_prefix() == b.get_safety_prefix() &&
        a.get_source()        == b.get_source() &&
        // a.get_source_name()   == b.get_source_name() &&
        a.get_seq()           == b.get_seq() &&
        a.get_seq_range()     == b.get_seq_range() &&
        a.get_aru_seq()       == b.get_aru_seq() &&
        a.get_flags()         == b.get_flags() && 
        a.get_source_view()   == b.get_source_view() &&
        a.get_gap()           == b.get_gap() &&
        a.get_fifo_seq()      == b.get_fifo_seq();
}

END_GCOMM_NAMESPACE


START_TEST(test_msg)
{
    UUID pid(4);
    EVSUserMessage umsg(pid, 0x10, 
                        SAFE, 0x037b137bU, 0x17U,
                        0x0534555,
                        ViewId(UUID(5), 0x7373b173U), EVSMessage::F_MSG_MORE);

    check_serialization(umsg, umsg.size(), 
                        EVSUserMessage(UUID(), 
                                       0, 
                                       UNRELIABLE, 
                                       0, 0, 0,
                                       ViewId(UUID(0,0), 0),
                                       0));


    EVSDelegateMessage dmsg(pid);
    check_serialization(dmsg, dmsg.size(), EVSDelegateMessage(UUID::nil()));
}
END_TEST


START_TEST(test_input_map_basic)
{
    EVSInputMap im;
    UUID pid1(1);
    UUID pid2(2);
    UUID pid3(3);
    // Test adding and removing instances
    im.insert_sa(pid1);
    im.insert_sa(pid2);
    im.insert_sa(pid3);

    try {
        im.insert_sa(pid2);
        fail("");
    } catch (FatalException e) {

    }

    im.erase_sa(pid2);

    try {
        im.erase_sa(pid2);
        fail("");
    } catch (FatalException e) {

    }
    im.clear();

    // Test message insert with one instance
    ViewId vid(pid1, 1);
    im.insert_sa(pid1);
    fail_unless(seqno_eq(im.get_aru_seq(), SEQNO_MAX) &&
                seqno_eq(im.get_safe_seq(), SEQNO_MAX));
    im.insert(
        EVSInputMapItem(pid1,
                        EVSUserMessage(pid1, 0x10, SAFE, 0, 0, SEQNO_MAX, vid, 0),
                        0, 0));
    fail_unless(seqno_eq(im.get_aru_seq(), 0));
    im.insert(EVSInputMapItem(pid1,
                             EVSUserMessage(pid1, 0x10, SAFE, 2, 0,
                                         SEQNO_MAX, vid, 0), 0, 0));
    fail_unless(seqno_eq(im.get_aru_seq(), 0));
    im.insert(EVSInputMapItem(pid1,
                              EVSUserMessage(pid1, 0x10, SAFE, 1, 0,
                                         SEQNO_MAX, vid, 0),
                              0, 0));
    fail_unless(seqno_eq(im.get_aru_seq(), 2));


    // Messge out of allowed window (approx aru_seq +- SEQNO_MAX/4)
    // must dropped:
    EVSRange gap = im.insert(
        EVSInputMapItem(pid1,
                        EVSUserMessage(pid1, 0x10, SAFE,
                                       seqno_add(2, SEQNO_MAX/4 + 1), 0, SEQNO_MAX, vid, 0),
                        0, 0));
    fail_unless(seqno_eq(gap.low, 3) && seqno_eq(gap.high, 2));
    fail_unless(seqno_eq(im.get_aru_seq(), 2));

    // Must not allow insertin second instance before clear()
    try {
        im.insert_sa(pid2);
        fail("");
    } catch (FatalException e) {

    }

    im.clear();

    // Simple two instance case

    im.insert_sa(pid1);
    im.insert_sa(pid2);

    for (uint32_t i = 0; i < 3; i++)
        im.insert(EVSInputMapItem(pid1,
                                  EVSUserMessage(pid1, 0x10,
                                                 SAFE, i, 0, SEQNO_MAX, vid, 0),
                                  0, 0));
    fail_unless(seqno_eq(im.get_aru_seq(), SEQNO_MAX));

    for (uint32_t i = 0; i < 3; i++) {
        im.insert(EVSInputMapItem(pid2,
                                  EVSUserMessage(pid2, 0x10, 
                                                 SAFE, i, 0, SEQNO_MAX, vid, 0),
                                  0, 0));
        fail_unless(seqno_eq(im.get_aru_seq(), i));
    }

    fail_unless(seqno_eq(im.get_safe_seq(), SEQNO_MAX));

    im.set_safe(pid1, 1);
    im.set_safe(pid2, 2);
    fail_unless(seqno_eq(im.get_safe_seq(), 1));

    im.set_safe(pid1, 2);
    fail_unless(seqno_eq(im.get_safe_seq(), 2));


    for (EVSInputMap::iterator i = im.begin(); i != im.end();
         ++i) {
        // std::cerr << i->get_sockaddr().to_string() << " " << i->get_evs_message().get_seq() << "\n";
    }

    EVSInputMap::iterator i_next;
    for (EVSInputMap::iterator i = im.begin(); i != im.end();
         i = i_next) {
        i_next = i;
        ++i_next;
        // std::cerr << i->get_sockaddr().to_string() << " " << i->get_evs_message().get_seq() << "\n";
        im.erase(i);
    }


    im.clear();

}
END_TEST


START_TEST(test_input_map_overwrap)
{

    set_seqno_max(EVS_SEQNO_MAX);

    EVSInputMap im;

    ViewId vid(UUID(), 3);
    static const size_t nodes = 16;
    static const size_t qlen = 8;
    UUID pids[nodes];
    for (size_t i = 0; i < nodes; ++i) {
        pids[i] = UUID(static_cast<int32_t>(i + 1));
        im.insert_sa(pids[i]);
    }

    Time start(Time::now());

    size_t n_msg = 0;
    for (uint32_t seqi = 0; seqi < 2*SEQNO_MAX; seqi++) {
        uint32_t seq = seqi % SEQNO_MAX;
// #define aru_seq SEQNO_MAX
#define aru_seq (seqi < 7 ? SEQNO_MAX : seqno_dec(im.get_aru_seq(), ::rand()%3))
        for (size_t j = 0; j < nodes; j++) {
            im.insert(EVSInputMapItem(pids[j],
                                      EVSUserMessage(
                                          pids[j],
                                          0x10,
                                          SAFE,
                                          seq, 0, aru_seq, vid, 0),
                                      0, 0));
            n_msg++;
        }

        if (seqi > 0 && seqi % qlen == 0) {
            uint32_t seqto = seqno_dec(
                seq, 
                (static_cast<uint32_t>(::rand() % qlen) + 1)*2);
            EVSInputMap::iterator mi_next;
            for (EVSInputMap::iterator mi = im.begin(); mi != im.end();
                 mi = mi_next) {
                     mi_next = mi;
                     ++mi_next;
                     if (seqno_lt(mi->get_evs_message().get_seq(), seqto))
                         im.erase(mi);
                     else if (seqno_eq(mi->get_evs_message().get_seq(), seqto)
&&
                              ::rand() % 8 != 0)
                        im.erase(mi);
                     else
                         break;
                 }
        }
    }
    EVSInputMap::iterator mi_next;
    for (EVSInputMap::iterator mi = im.begin(); mi != im.end();
         mi = mi_next) {
             mi_next = mi;
             ++mi_next;
             im.erase(mi);
         }
    Time stop(Time::now());
    std::cerr << "Msg rate " << double(n_msg)/(stop.to_double() - start.to_double()) <<
"\n";
    set_seqno_max(0);

}
END_TEST


START_TEST(test_input_map_random)
{
    set_seqno_max(EVS_SEQNO_MAX);
    // Construct set of messages to be inserted

    // Fetch messages randomly and insert to input map

    // Iterate over input map - outcome must be
    UUID pid(0, 0);
    ViewId vid(pid, 3);
    vector<EVSMessage> msgs(SEQNO_MAX/4);

    for (uint32_t i = 0; i < SEQNO_MAX/4; ++i)
        msgs[i] = EVSUserMessage(
            pid,
            0x10,
            SAFE,
            i,
            0,
            SEQNO_MAX,
            vid,
            0);

    EVSInputMap im;
    vector<UUID> pids(4);
    for (size_t i = 0; i < 4; ++i)
    {
        pids[i] = UUID(static_cast<int32_t>(i + 1));
        im.insert_sa(pids[i]);
    }
    
    for (size_t i = 0; i < 4; ++i) {
        for (size_t j = msgs.size(); j > 0; --j) {
            size_t n = ::rand() % j;
            im.insert(EVSInputMapItem(pids[i], msgs[n], 0, 0));
            std::swap(msgs[n], msgs[j - 1]);
        }
    }

    size_t cnt = 0;
    for (EVSInputMap::iterator i = im.begin();
         i != im.end(); ++i) {
        fail_unless(i->get_sockaddr() == pids[cnt % 4]);
        fail_unless(seqno_eq(i->get_evs_message().get_seq(), static_cast<uint32_t>(cnt/4)));
        ++cnt;
    }

    set_seqno_max(0);
}
END_TEST


class DummyUser : public Toplay
{
    uint32_t deliv_seq;
public:
    DummyUser() :
        deliv_seq(SEQNO_MAX)
    {
        
    }

    void send(const uint32_t seq)
    {
        byte_t buf[sizeof(seq)];
        fail_unless(make_int(seq).write(buf, sizeof(buf), 0) != 0);
        WriteBuf wb(buf, sizeof(buf));
        int err;
        if ((err = pass_down(&wb, 0)) != 0)
        {
            LOG_WARN("dummy user, pass down: (" + make_int(err).to_string() 
                     + "): '" + ::strerror(err) + "'");
        }
    }
    
    void handle_up(const int cid, const ReadBuf* rb, const size_t roff,
                   const ProtoUpMeta* um)
    {
        if (rb)
        {
            IntType<uint32_t> rseq(SEQNO_MAX);
            fail_unless(rseq.read(rb->get_buf(), rb->get_len(), roff) != 0);
            deliv_seq = rseq.get();
        }
    }
    
    uint32_t get_deliv_seq() const
    {
        return deliv_seq;
    }
};



/* Read EVSMessage and release rb */
static void get_msg(ReadBuf* rb, EVSMessage* msg, bool release = true)
{
    assert(msg != 0);
    if (rb == 0)
    {
        log_info << "get_msg: (null)";
    }
    else
    {
        fail_unless(msg->read(rb->get_buf(), rb->get_len(), 0) != 0);
        log_info << "get_msg: " << msg->to_string();
        if (release)
            rb->release();
    }
}

class InputEvent
{
    const View view;
    const UUID source;
    const uint32_t seq;
public:
    InputEvent(const View& view_, const UUID& source_, const uint32_t seq_) :
        view(view_),
        source(source_),
        seq(seq_)
    {
    }

    const View& get_view() const
    {
        return view;
    }

    const UUID& get_source() const
    {
        return source;
    }

    uint32_t get_seq() const
    {
        return seq;
    }

};

class DummyInstance : public Toplay
{
    DummyTransport tp;
    EVSProto ep;
    uint32_t send_seq;
    std::queue<InputEvent> input;
    DummyInstance(const DummyInstance&);
    void operator=(const DummyInstance&);
public:
    DummyInstance(EventLoop* el, const int32_t idx, const string& name) :
        tp(),
        ep(el, &tp, UUID(idx), name, 0),
        send_seq(0),
        input()
    {
        gcomm::connect(&tp, &ep);
        gcomm::connect(&ep, this);
    }
    
    ~DummyInstance()
    {
        gcomm::disconnect(&tp, &ep);
        gcomm::disconnect(&ep, this);
    }
    
    
    void push_input(const InputEvent& ev)
    {
        input.push(ev);
    }

    void handle_up(const int cid, const ReadBuf* rb, const size_t roff, 
                   const ProtoUpMeta* um)
    {
        const View* view = um->get_view();
        if (view)
        {
            log_debug << view->to_string();
        }
    }

    


    void assert_state(const EVSProto::State state)
    {
        fail_unless(ep.get_state() == state, "expected %s real %s: %s", 
                    EVSProto::to_string(state).c_str(),
                    EVSProto::to_string(ep.get_state()).c_str(),
                    ep.to_string().c_str());
    }

    void assert_send(const EVSMessage::Type type, 
                     const EVSProto::State before_state,
                     const EVSProto::State after_state,
                     const bool send_flags = true)
    {
        assert_state(before_state);
        switch (type)
        {
        case EVSMessage::JOIN:
            ep.send_join(send_flags);
            break;
        case EVSMessage::LEAVE:
            ep.send_leave();
            break;
        case EVSMessage::USER:
        {
            byte_t buf[4];
            gcomm::write(send_seq, buf, sizeof(buf), 0);
            WriteBuf wb(buf, sizeof(buf));
            int err;
            if ((err = pass_down(&wb, 0)) != 0)
            {
                fail("send failed with error code %i", err);
            }
            send_seq++;
            break;
        }   
        default:
            fail("not implemented");
        }
        assert_state(after_state);
    }
    
    /* EVSProto must not have any output */
    void assert_output_empty()
    {
        EVSMessage msg;
        ReadBuf* rb = tp.get_out();
        if (rb != 0)
        {
            get_msg(rb, &msg);
        }
        fail_unless(rb == 0, "%s", msg.to_string().c_str());
    }
    
    /* EVSProto must have output with given type and following state */
    void assert_output_msg(EVSMessage* msg, 
                           const EVSMessage::Type type, 
                           const EVSProto::State state)
    {
        assert_state(state);
        ReadBuf* rb = tp.get_out();
        fail_unless(rb != 0);
        get_msg(rb, msg);
        fail_unless(msg->get_type() == type, "%i %i", msg->get_type(), type);
        assert_state(state);
    }
    
    /* EVSProto must accept given message and shift to following state */
    void assert_input_msg(const EVSMessage& msg,
                          const EVSProto::State state)
    {
        ep.handle_msg(msg);
        assert_state(state);
    }

    void input_msg(const EVSMessage& msg, const ReadBuf* rb = 0, 
                   const size_t roff = 0)
    {
        ep.handle_msg(msg, rb, roff);
    }

    void assert_shift_to(const EVSProto::State from_state,
                         const EVSProto::State to_state)
    {
        assert_state(from_state);
        ep.shift_to(to_state);
        assert_state(to_state);
    }

    ReadBuf* get_output()
    {
        return tp.get_out();
    }
    
    const UUID& get_uuid() const
    {
        return ep.get_uuid();
    }

    void set_inactive(const UUID& uuid)
    {
        ep.set_inactive(uuid);
    }

    void check_inactive()
    {
        ep.check_inactive();
    }

    void send_keepalive()
    {
        if (ep.is_output_empty())
        {
            WriteBuf wb(0, 0);
            ep.send_user(&wb, 0xf, DROP, 16, SEQNO_MAX);
        }
        else
        {
            ep.send_user();
        }
    }

    void shift_to(const EVSProto::State state, const bool send_j = false)
    {
        ep.shift_to(state, send_j);
    }
    
};

struct name_cmp_str
{
    bool operator()(const DummyInstance* a, const DummyInstance* b) const
    {
        return a->get_uuid() < b->get_uuid();
    }
};

typedef std::set<DummyInstance*, name_cmp_str> DummyList;

static void single_boot(DummyInstance* di)
{
    EVSMessage msg;

    di->assert_shift_to(EVSProto::S_CLOSED, EVSProto::S_JOINING);
    di->assert_send(EVSMessage::JOIN, EVSProto::S_JOINING, EVSProto::S_OPERATIONAL);
    di->assert_output_msg(&msg, EVSMessage::JOIN, EVSProto::S_OPERATIONAL);
    di->assert_output_msg(&msg, EVSMessage::INSTALL, EVSProto::S_OPERATIONAL);
    di->assert_output_msg(&msg, EVSMessage::GAP, EVSProto::S_OPERATIONAL);
}


struct assert_state
{
    EVSProto::State state;
    assert_state(const EVSProto::State state_) : 
        state(state_)
    {
    }
    void operator()(DummyInstance* di)
    {
        di->assert_state(state);
    }
};

static void multicast(DummyList& dlist, DummyList::iterator& src, 
                      const ReadBuf* rb)
{
    EVSMessage msg;
    fail_unless(msg.read(rb->get_buf(), rb->get_len(), 0) != 0);
    for (DummyList::iterator j = dlist.begin();
         j != dlist.end(); ++j)
    {
        if (src != j)
        {
            (*j)->input_msg(msg, rb, 0);
        }
    }
}

static void reach_operational(DummyList& dlist, const bool send_join = false)
{
    bool empty;
    do
    {
        empty = true;
        for (DummyList::iterator i = dlist.begin(); i != dlist.end(); ++i)
        {
            ReadBuf* rb = (*i)->get_output();
            if (rb != 0)
            {
                multicast(dlist, i, rb);
                rb->release();
                empty = false;
            }
        }
    }
    while (empty == false);
    for (DummyList::iterator i = dlist.begin(); i != dlist.end(); ++i)
    {
        ReadBuf* rb = (*i)->get_output();
        if (rb != 0)
        {
            throw std::logic_error("");
        }
    }
    log_info << "reach operational, checking states";
    std::for_each(dlist.begin(), dlist.end(), assert_state(EVSProto::S_OPERATIONAL));
}

static void join_instance(DummyList& dlist, DummyInstance* di)
{
    if (dlist.size() == 0)
    {
        dlist.insert(di);
        single_boot(*dlist.begin());
    }
    else
    {
        std::for_each(dlist.begin(), dlist.end(), assert_state(EVSProto::S_OPERATIONAL));
        di->assert_shift_to(EVSProto::S_CLOSED, EVSProto::S_JOINING);
        di->assert_send(EVSMessage::JOIN, EVSProto::S_JOINING, 
                        EVSProto::S_JOINING, false);
        dlist.insert(di);
        reach_operational(dlist);
    }
}

struct set_inactive_in
{
    const DummyList& dlist;
    set_inactive_in(const DummyList& dlist_) : 
        dlist(dlist_) {}
    void operator()(DummyInstance* di)
    {
        for (DummyList::const_iterator i = dlist.begin(); i != dlist.end(); ++i)
        {
            di->set_inactive((*i)->get_uuid());
        }
    }
};

struct check_inactive
{
    void operator()(DummyInstance* di)
    {
        di->check_inactive();
    }
};

struct init_recovery
{
    void operator()(DummyInstance* di)
    {
        di->shift_to(EVSProto::S_RECOVERY, true);
    }
};

struct send_msgs
{
    int n;
    send_msgs(const int n_) : n(n_) {}
    void operator()(DummyInstance* di)
    {
        for (int i = 0; i < n; ++i)
        {
            di->assert_send(EVSMessage::USER, EVSProto::S_OPERATIONAL,
                            EVSProto::S_OPERATIONAL);
        }
    }
};

static void split_instances(DummyList& dlist, const size_t n, DummyList& ret)
{
    if (n >= dlist.size())
    {
        throw std::logic_error("");
    }
    size_t orig_size = dlist.size();
    for (size_t i = 0; i < n; ++i)
    {
        DummyList::iterator d_iter = dlist.begin();
        if (d_iter == dlist.end())
        {
            throw std::logic_error("node not found");
        }
        ret.insert(*d_iter);
        dlist.erase(d_iter);
    }
    if (ret.size() != n ||
        orig_size != dlist.size() + n)
    {
        throw std::logic_error(" ");
    }
    
    std::for_each(dlist.begin(), dlist.end(), set_inactive_in(ret));
    std::for_each(ret.begin(), ret.end(), set_inactive_in(dlist));
    
    std::for_each(dlist.begin(), dlist.end(), check_inactive());
    std::for_each(ret.begin(), ret.end(), check_inactive());
    
    std::for_each(dlist.begin(), dlist.end(), assert_state(EVSProto::S_RECOVERY));
    std::for_each(ret.begin(), ret.end(), assert_state(EVSProto::S_RECOVERY));

    if (dlist.size() == 1)
    {
        (*dlist.begin())->assert_send(EVSMessage::JOIN, 
                                      EVSProto::S_RECOVERY,
                                      EVSProto::S_OPERATIONAL,
                                      true);
    }
    if (ret.size() == 1)
    {
        (*ret.begin())->assert_send(EVSMessage::JOIN, 
                                    EVSProto::S_RECOVERY,
                                    EVSProto::S_OPERATIONAL,
                                    true);
    }
    reach_operational(dlist);
    reach_operational(ret);
}

static void merge_instances(DummyList& dlist1, DummyList& dlist2)
{
    size_t tot_size = dlist1.size() + dlist2.size();

    while (dlist2.empty() == false)
    {
        dlist1.insert(*dlist2.begin());
        dlist2.erase(dlist2.begin());
    }
    if (dlist1.size() != tot_size || dlist2.size() != 0)
    {
        throw std::logic_error(" ");
    }
    
    std::for_each(dlist1.begin(), dlist1.end(), 
                  assert_state(EVSProto::S_OPERATIONAL));
    std::for_each(dlist1.begin(), dlist1.end(), init_recovery());
    
    reach_operational(dlist1);
}

static void propagate_up_to(DummyList& dlist, const vector<int>& vec)
{
    vector<int> cnt(vec);
    if (cnt.size() == 0 || dlist.size() > cnt.size())
    {
        throw std::logic_error("");
    }
    while (*std::max_element(cnt.begin(), cnt.end()) > 0)
    {
        int idx = 0;
        for (DummyList::iterator i = dlist.begin(); i != dlist.end(); ++i)
        {
            if (cnt[idx] > 0)
            {
                ReadBuf* rb = (*i)->get_output();
                if (rb != 0)
                {
                    multicast(dlist, i, rb);
                    rb->release();
                }
                else
                {
                    log_warn << "no output available";
                }
            }
            --cnt[idx];
            ++idx;
        }
    }
}


START_TEST(test_evs_proto_generic_boot)
{
    EventLoop el;
    DummyList dlist;
    const int n_nodes = 4;
    const int n_msgs = 2;
    vector<UUID> uuids;
    

    for (int i = 0; i < n_nodes; ++i)
    {
        join_instance(dlist, 
                      new DummyInstance(&el, i + 1, "n" + make_int(i + 1).to_string()));
    }
    
    /* */
    

    
    // gu_log_max_level = GU_LOG_DEBUG;
    for (int n = 0; n < n_msgs; ++n)
    {
        std::vector<int> perm(n_nodes, 0);
        do
        {
            for (int i = 1; i < n_nodes; ++i)
            {
                std::cerr << "Permutation: ";
                std::copy(perm.begin(), perm.end(), 
                          std::ostream_iterator<int>(std::cerr, " "));
                std::cerr << "msgs: " << n << " split: " << i;
                std::cerr << std::endl;
                std::for_each(dlist.begin(), dlist.end(), send_msgs(n));
                propagate_up_to(dlist, perm);
                log_info << "splitting: " << i;
                DummyList dlist2;
                split_instances(dlist, i, dlist2);
                log_info << "merging: " << i;
                merge_instances(dlist, dlist2);
            }
        }
        while (boost::next_mapping(perm.begin(), perm.end(), 0, n + 1));
    }
    std::for_each(dlist.begin(), dlist.end(), delete_object());
}
END_TEST


static void single_boot(DummyTransport* tp, EVSProto* ep)
{
    EVSMessage jm;
    EVSMessage im;
    EVSMessage gm;

    // Initial state is joining
    ep->shift_to(EVSProto::S_JOINING);

    // Send join must produce emitted join message
    ep->send_join();
    ReadBuf* rb = tp->get_out();
    fail_unless(rb != 0);
    get_msg(rb, &jm);
    fail_unless(jm.get_type() == EVSMessage::JOIN);

    // Install message is emitted at the end of JOIN handling
    // 'cause this is the only instance and is always consistent
    // with itself
    rb = tp->get_out();
    fail_unless(rb != 0);
    get_msg(rb, &im);
    fail_unless(im.get_type() == EVSMessage::INSTALL);

    // Handling INSTALL message must emit gap message
    rb = tp->get_out();
    fail_unless(rb != 0);
    get_msg(rb, &gm);
    fail_unless(gm.get_type() == EVSMessage::GAP);

    // State must have evolved JOIN -> S_RECOVERY -> S_OPERATIONAL
    fail_unless(ep->get_state() == EVSProto::S_OPERATIONAL);


    // Handle join message again, must stay in S_OPERATIONAL, must not
    // emit anything
    ep->handle_msg(jm);
    rb = tp->get_out();
    get_msg(rb, &gm);
    fail_unless(rb == 0);
    fail_unless(ep->get_state() == EVSProto::S_OPERATIONAL);
}


START_TEST(test_evs_proto_single_boot)
{
    EventLoop el;
    UUID pid(1);
    DummyTransport* tp = new DummyTransport();
    DummyUser du;
    EVSProto* ep = new EVSProto(&el, tp, pid, "n1", 0);
    connect(tp, ep);
    connect(ep, &du);
    
    single_boot(tp, ep);
    
    delete ep;
    delete tp;
}
END_TEST

static void double_boot(DummyTransport* tp1, EVSProto* ep1, 
                        DummyTransport* tp2, EVSProto* ep2)
{

    single_boot(tp1, ep1);

    EVSMessage jm;
    EVSMessage im;
    EVSMessage gm;
    EVSMessage gm2;
    EVSMessage msg;

    ReadBuf* rb;

    ep2->shift_to(EVSProto::S_JOINING);
    fail_unless(ep1->get_state() == EVSProto::S_OPERATIONAL);
    fail_unless(ep2->get_state() == EVSProto::S_JOINING);

    // Send join message, don't handle immediately
    ep2->send_join(false);
    fail_unless(ep2->get_state() == EVSProto::S_JOINING);
    rb = tp2->get_out();
    fail_unless(rb != 0);
    get_msg(rb, &jm);
    fail_unless(jm.get_type() == EVSMessage::JOIN);
    rb = tp2->get_out();
    fail_unless(rb == 0);

    ep1->handle_msg(jm);
    fail_unless(ep1->get_state() == EVSProto::S_RECOVERY);

    rb = tp1->get_out();
    fail_unless(rb != 0);
    get_msg(rb, &jm);
    fail_unless(jm.get_type() == EVSMessage::JOIN);

    rb = tp1->get_out();
    get_msg(rb, &msg);
    fail_unless(rb == 0);

    ep2->handle_msg(jm);
    fail_unless(ep2->get_state() == EVSProto::S_RECOVERY);
    rb = tp2->get_out();
    fail_unless(rb != 0);
    get_msg(rb, &jm);
    fail_unless(jm.get_type() == EVSMessage::JOIN);
    rb = tp2->get_out();
    get_msg(rb, &msg);
    fail_unless(rb == 0);

    ep1->handle_msg(jm);
    fail_unless(ep1->get_state() == EVSProto::S_RECOVERY);
    rb = tp1->get_out();
    fail_unless(rb != 0);
    get_msg(rb, &im);
    fail_unless(im.get_type() == EVSMessage::INSTALL);

    rb = tp1->get_out();
    fail_unless(rb != 0);
    get_msg(rb, &gm);
    fail_unless(gm.get_type() == EVSMessage::GAP);
    rb = tp1->get_out();
    fail_unless(rb == 0);

    ep2->handle_msg(im);
    fail_unless(ep2->get_state() == EVSProto::S_RECOVERY);
    rb = tp2->get_out();
    fail_unless(rb != 0);
    get_msg(rb, &gm2);
    fail_unless(gm2.get_type() == EVSMessage::GAP);

    rb = tp2->get_out();
    get_msg(rb, &msg);
    fail_unless(rb == 0);

    ep1->handle_msg(gm2);
    fail_unless(ep1->get_state() == EVSProto::S_OPERATIONAL);
    rb = tp1->get_out();
    fail_unless(rb == 0);

    ep2->handle_msg(gm);
    fail_unless(ep2->get_state() == EVSProto::S_OPERATIONAL);
    rb = tp2->get_out();
    fail_unless(rb == 0);
}

START_TEST(test_evs_proto_double_boot)
{
    EventLoop el;
    UUID p1(1);
    UUID p2(2);


    DummyTransport* tp1 = new DummyTransport();
    DummyUser du1;
    EVSProto* ep1 = new EVSProto(&el, tp1, p1, "n1", 0);
    
    connect(tp1, ep1);
    connect(ep1, &du1);
    
    DummyUser du2;
    DummyTransport* tp2 = new DummyTransport();
    EVSProto* ep2 = new EVSProto(&el, tp2, p2, "n2", 0);
    
    connect(tp2, ep2);
    connect(ep2, &du2);
    
    
    double_boot(tp1, ep1, tp2, ep2);

    delete ep1;
    delete ep2;
    delete tp1;
    delete tp2;
}
END_TEST

START_TEST(test_evs_proto_user_msg_basic)
{
    EventLoop el;
    UUID p1(1);
    UUID p2(2);


    DummyTransport* tp1 = new DummyTransport();
    DummyUser du1;
    EVSProto* ep1 = new EVSProto(&el, tp1, p1, "n1", 0);
    
    connect(tp1, ep1);
    connect(ep1, &du1);
    
    DummyUser du2;
    DummyTransport* tp2 = new DummyTransport();
    EVSProto* ep2 = new EVSProto(&el, tp2, p2, "n2", 0);
    
    connect(tp2, ep2);
    connect(ep2, &du2);

    double_boot(tp1, ep1, tp2, ep2);


    ReadBuf* rb;
    ReadBuf* rb_n;

    du1.send(0);
    
    EVSMessage um1;
    rb = tp1->get_out();
    get_msg(rb, &um1, false);
    fail_unless(rb != 0);
    fail_unless(um1.get_type() == EVSMessage::USER);
    fail_unless(um1.get_seq() == 0);
    fail_unless(um1.get_aru_seq() == SEQNO_MAX);
    
    rb_n = tp1->get_out();
    fail_unless(rb_n == 0);
    
    fail_unless(du1.get_deliv_seq() == SEQNO_MAX);
    
    EVSMessage um2;
    
    ep2->handle_msg(um1, rb, 0);
    rb->release();
    rb = tp2->get_out();
    get_msg(rb, &um2, false);
    fail_unless(rb != 0);
    fail_unless(um2.get_type() == EVSMessage::USER);
    fail_unless(um2.get_seq() == 0);
    fail_unless(um2.get_aru_seq() == 0);
    
    rb_n = tp2->get_out();
    fail_unless(rb_n == 0);
    fail_unless(du2.get_deliv_seq() == SEQNO_MAX);
    
    EVSMessage gm1;
    ep1->handle_msg(um2, rb, 0);
    rb->release();
    rb = tp1->get_out();
    get_msg(rb, &gm1);
    fail_unless(rb != 0);
    fail_unless(gm1.get_type() == EVSMessage::GAP);
    fail_unless(gm1.get_aru_seq() == 0);
    rb_n = tp1->get_out();
    fail_unless(rb_n == 0);
    
    fail_unless(du1.get_deliv_seq() == 0);
    
    ep2->handle_msg(gm1);
    rb = tp2->get_out();
    fail_unless(rb == 0);
    
    fail_unless(du2.get_deliv_seq() == 0);
    
    delete ep1;
    delete ep2;
    delete tp1;
    delete tp2;    
}
END_TEST

START_TEST(test_evs_proto_leave_basic)
{
    EventLoop el;
    UUID p1(1);
    UUID p2(2);


    DummyTransport* tp1 = new DummyTransport();
    DummyUser du1;
    EVSProto* ep1 = new EVSProto(&el, tp1, p1, "n1", 0);
    
    connect(tp1, ep1);
    connect(ep1, &du1);
    
    DummyUser du2;
    DummyTransport* tp2 = new DummyTransport();
    EVSProto* ep2 = new EVSProto(&el, tp2, p2, "n2", 0);
    
    connect(tp2, ep2);
    connect(ep2, &du2);

    double_boot(tp1, ep1, tp2, ep2);

    ReadBuf* rb;
    
    EVSMessage lm1;
    
    ep1->shift_to(EVSProto::S_LEAVING);
    ep1->send_leave();
    fail_unless(ep1->get_state() == EVSProto::S_CLOSED);
    rb = tp1->get_out();
    fail_unless(rb != 0);
    get_msg(rb, &lm1);
    fail_unless(lm1.get_type() == EVSMessage::LEAVE);
    rb = tp1->get_out();
    fail_unless(rb == 0);

    EVSMessage jm2;
    EVSMessage im2;
    EVSMessage gm2;
    ep2->handle_msg(lm1);
    rb = tp2->get_out();
    fail_unless(rb != 0);
    get_msg(rb, &jm2);
    fail_unless(jm2.get_type() == EVSMessage::JOIN);
    rb = tp2->get_out();
    fail_unless(rb != 0);
    get_msg(rb, &im2);
    fail_unless(im2.get_type() == EVSMessage::INSTALL);
    fail_unless(ep2->get_state() == EVSProto::S_OPERATIONAL);
    rb = tp2->get_out();
    fail_unless(rb != 0);
    get_msg(rb, &gm2);
    fail_unless(gm2.get_type() == EVSMessage::GAP);
    rb = tp2->get_out();
    fail_unless(rb == 0);

    delete ep1;
    delete ep2;
    delete tp1;
    delete tp2;
}
END_TEST


START_TEST(test_evs_proto_duplicates)
{
    EventLoop el;
    UUID p1(1);
    UUID p2(2);

    DummyTransport* tp1 = new DummyTransport();
    DummyUser du1;
    EVSProto* ep1 = new EVSProto(&el, tp1, p1, "n1", 0);
    
    connect(tp1, ep1);
    connect(ep1, &du1);
    
    DummyUser du2;
    DummyTransport* tp2 = new DummyTransport();
    EVSProto* ep2 = new EVSProto(&el, tp2, p2, "n2", 0);
    
    connect(tp2, ep2);
    connect(ep2, &du2);

    single_boot(tp1, ep1);
    
    EVSMessage jm1, jm2;
    EVSMessage im;
    EVSMessage gm;
    EVSMessage gm2;
    EVSMessage msg;
    
    ReadBuf* rb;
    
    ep2->shift_to(EVSProto::S_JOINING);
    fail_unless(ep1->get_state() == EVSProto::S_OPERATIONAL);
    fail_unless(ep2->get_state() == EVSProto::S_JOINING);
    
    // Send join message, don't handle immediately
    ep2->send_join(false);
    fail_unless(ep2->get_state() == EVSProto::S_JOINING);
    rb = tp2->get_out();
    fail_unless(rb != 0);
    get_msg(rb, &jm2);
    fail_unless(jm2.get_type() == EVSMessage::JOIN);
    rb = tp2->get_out();
    fail_unless(rb == 0);

    ep1->handle_msg(jm2);
    fail_unless(ep1->get_state() == EVSProto::S_RECOVERY);

    rb = tp1->get_out();
    fail_unless(rb != 0);
    get_msg(rb, &jm1);
    fail_unless(jm1.get_type() == EVSMessage::JOIN);

    rb = tp1->get_out();
    get_msg(rb, &msg);
    fail_unless(rb == 0);

    ep2->handle_msg(jm1);
    fail_unless(ep2->get_state() == EVSProto::S_RECOVERY);
    rb = tp2->get_out();
    fail_unless(rb != 0);
    get_msg(rb, &jm2);
    fail_unless(jm2.get_type() == EVSMessage::JOIN);
    rb = tp2->get_out();
    get_msg(rb, &msg);
    fail_unless(rb == 0);

    ep1->handle_msg(jm2);
    ep1->handle_msg(jm1);
    ep1->handle_msg(jm2);
    fail_unless(ep1->get_state() == EVSProto::S_RECOVERY);
    rb = tp1->get_out();
    fail_unless(rb != 0);
    get_msg(rb, &im);
    fail_unless(im.get_type() == EVSMessage::INSTALL);
    
    rb = tp1->get_out();
    fail_unless(rb != 0);
    get_msg(rb, &gm);
    fail_unless(gm.get_type() == EVSMessage::GAP);
    rb = tp1->get_out();
    fail_unless(rb == 0);
    
    ep1->handle_msg(jm2);
    ep1->handle_msg(jm1);
    ep1->handle_msg(jm2);
    ep1->handle_msg(im);
    fail_unless(ep1->get_state() == EVSProto::S_RECOVERY);
    
    ep2->handle_msg(im);
    fail_unless(ep2->get_state() == EVSProto::S_RECOVERY);
    rb = tp2->get_out();
    fail_unless(rb != 0);
    get_msg(rb, &gm2);
    fail_unless(gm2.get_type() == EVSMessage::GAP);

    rb = tp2->get_out();
    get_msg(rb, &msg);
    fail_unless(rb == 0);

    ep1->handle_msg(gm2);
    fail_unless(ep1->get_state() == EVSProto::S_OPERATIONAL);
    rb = tp1->get_out();
    fail_unless(rb == 0);

    ep2->handle_msg(gm);
    fail_unless(ep2->get_state() == EVSProto::S_OPERATIONAL);
    rb = tp2->get_out();
    fail_unless(rb == 0);

    ep1->handle_msg(jm2);
    ep1->handle_msg(jm1);
    ep1->handle_msg(jm2);
    ep1->handle_msg(im);
    fail_unless(ep1->get_state() == EVSProto::S_OPERATIONAL);

    
    ep2->handle_msg(jm2);
    ep2->handle_msg(jm1);
    ep2->handle_msg(jm2);
    ep2->handle_msg(im);
    fail_unless(ep2->get_state() == EVSProto::S_OPERATIONAL);

    delete ep1;
    delete ep2;
    delete tp1;
    delete tp2;
}
END_TEST



struct Inst : public Toplay {
private:
    Inst(const Inst&);
    void operator=(const Inst&);
public:
    DummyTransport* tp;
    EVSProto* ep;
    uint32_t sent_seq;
    uint32_t delivered_seq;
    Inst(DummyTransport* tp_, EVSProto* ep_) : 
        tp(tp_), 
        ep(ep_),
        sent_seq(0),
        delivered_seq(0)
    {
        connect(tp, ep);
        connect(ep, this);
    }

    ~Inst()
    {
        // check_completeness();
        delete ep;
        delete tp;
    }
    
    void check_completeness()
    {
        fail_unless(sent_seq == delivered_seq, "sent %u delivered %u",
                    sent_seq, delivered_seq);
    }

    void handle_up(const int ctx, const ReadBuf* rb, const size_t roff,
                   const ProtoUpMeta* um)
    {
        fail_unless(um != 0);
        if (um->get_source() == ep->get_uuid())
        {
            IntType<uint32_t> rseq;
            fail_unless(rseq.read(rb->get_buf(), rb->get_len(), roff) != 0);
            if (rseq.get() != delivered_seq + 1)
            {
                log_error << "error in msg sequence: got " 
                          << rseq.get() 
                          << " expected "
                          << delivered_seq;
            }
            fail_unless(rseq.get() == delivered_seq + 1);
            ++delivered_seq;
        }
    }
    
    void send(WriteBuf* wb)
    {
        if (ep->get_state() != EVSProto::S_OPERATIONAL)
        {
            return;
        }
        byte_t buf[4];
        if (make_int(sent_seq + 1).write(buf, sizeof(buf), 0) == 0)
        {
            throw FatalException("");
        }
        wb->prepend_hdr(buf, sizeof(buf));
        if (pass_down(wb, 0) == 0)
        {
            ++sent_seq;
        }
        wb->rollback_hdr(sizeof(buf));
    }

};

static bool all_operational(const vector<Inst*>* pvec)
{
    for (vector<Inst*>::const_iterator i = pvec->begin();
         i != pvec->end(); ++i) {
        if ((*i)->ep->get_state() != EVSProto::S_OPERATIONAL &&
            (*i)->ep->get_state() != EVSProto::S_CLOSED)
            return false;
    }
    return true;
}

struct Stats {
    uint64_t sent_msgs;
    uint64_t total_msgs;
    vector<uint64_t> msgs;

    Stats() : 
        sent_msgs(0), 
        total_msgs(0),
        msgs()
    {
        msgs.resize(7, 0);
    }

    ~Stats() {
        // if (total_msgs) {
        //  print();
        // }
    }

    static Double fraction_of(const uint64_t a, const uint64_t b) 
    {
        if (b == 0)
            return 0;
        else
            return double(a)/double(b);
    }
    
    void print() 
    {
        LOG_INFO("Sent messages: " + make_int(sent_msgs).to_string());
        LOG_INFO("Total messages: " + make_int(total_msgs).to_string());
        for (size_t i = 0; i < 6; ++i) {
            LOG_INFO("Type " + EVSMessage::to_string(static_cast<EVSMessage::Type>(i))
                     + " messages: " + make_int(msgs[i]).to_string()
                     + " fraction of sent: " + fraction_of(msgs[i], sent_msgs).to_string()
                     + " fraction of total: " + fraction_of(msgs[i], total_msgs).to_string());
    }
}

    void clear() {
        sent_msgs = 0;
        total_msgs = 0;
        for (size_t i = 0; i < 6; ++i)
           msgs[i] = 0;
    }

    void acc_mcast(const int type) {
        if (type < 0 || type > 6)
        {
            throw FatalException("");
        }
        total_msgs++;
        msgs[type]++;
    }

    void acc_sent() {
        sent_msgs++;
    }

};

static Stats stats;

static void multicast(vector<Inst*>* pvec, const ReadBuf* rb, const int ploss)
{
    EVSMessage msg;
    fail_unless(msg.read(rb->get_buf(), rb->get_len(), 0) != 0);
    LOG_DEBUG("msg: " + make_int(msg.get_type()).to_string());
    stats.acc_mcast(msg.get_type());
    for (vector<Inst*>::iterator j = pvec->begin();
         j != pvec->end(); ++j) {

        if ((*j)->ep->get_state() == EVSProto::S_CLOSED)
            continue;

        if (::rand() % 10000 < ploss) {
            if (::rand() % 3 == 0) {
                LOG_DEBUG("dropping " + EVSMessage::to_string(msg.get_type()) +
                         " from " + msg.get_source().to_string() + " to " +
                          (*j)->ep->get_uuid().to_string() 
                         + " seq " + make_int(msg.get_seq()).to_string());
                continue;
            } else {
                LOG_DEBUG("dropping " + EVSMessage::to_string(msg.get_type()) +" from " + msg.get_source().to_string() + " to all");
                break;
            }
        }

        (*j)->ep->handle_msg(msg, rb, 0);
    }
}

static void reach_operational(vector<Inst*>* pvec)
{
    size_t cons_tout_cnt = 0;
    while (all_operational(pvec) == false) 
    {
        for (vector<Inst*>::iterator i = pvec->begin();
             i != pvec->end(); ++i) {
            ReadBuf* rb = (*i)->tp->get_out();
            if (rb) {
                multicast(pvec, rb, 0);
                rb->release();
            }
        }
        if (++cons_tout_cnt > 1000)
        {
            for (vector<Inst*>::iterator i = pvec->begin();
                 i != pvec->end(); ++i)
            {
                (*i)->ep->consth->handle();                
                (*i)->ep->ith->handle();
                (*i)->ep->resendth->handle();
            }
            cons_tout_cnt = 0;
        }
    }
}

static void flush(vector<Inst*>* pvec)
{


    bool not_empty ;
    do
    {
        not_empty = false;
        for (vector<Inst*>::iterator i = pvec->begin();
             i != pvec->end(); ++i) {
            ReadBuf* rb = (*i)->tp->get_out();
            if (rb) 
            {
                not_empty = true;
                multicast(pvec, rb, 0);
                rb->release();
            }
        }
        if (not_empty == false)
        {
            for (vector<Inst*>::iterator i = pvec->begin();
                 i != pvec->end(); ++i) 
            {        
                if ((*i)->ep->is_output_empty() == false)
                {
                    (*i)->ep->resendth->handle();
                    not_empty = true;
                }
            }
        }
    }
    while (not_empty == true);

    // Generate internal EVS message to increase probability that 
    // all user messages are delivered when this terminates
    for (vector<Inst*>::iterator i = pvec->begin();
         i != pvec->end(); ++i)
    {
        (*i)->ep->resendth->handle();
    }

    do
    {
        not_empty = false;
        for (vector<Inst*>::iterator i = pvec->begin();
             i != pvec->end(); ++i) {
            ReadBuf* rb = (*i)->tp->get_out();
            if (rb) 
            {
                not_empty = true;
                multicast(pvec, rb, 0);
                rb->release();
            }
        }
    }
    while (not_empty == true);


    for (vector<Inst*>::iterator i = pvec->begin();
         i != pvec->end(); ++i) 
    {
        (*i)->check_completeness();
    }
    LOG_INFO("FLUSH");
}

START_TEST(test_evs_proto_converge)
{
    EventLoop el;
    size_t n = 8;
    vector<Inst*> vec(n);

    for (size_t i = 0; i < n; ++i) {
        DummyTransport* tp = new DummyTransport();
        vec[i] = new Inst(tp, new EVSProto(&el, tp, UUID(static_cast<int32_t>(i + 1)),
                                           "n" + make_int(i + 1).to_string(), 0));
        vec[i]->ep->shift_to(EVSProto::S_JOINING);
        vec[i]->ep->send_join(false);
    }
    reach_operational(&vec);
    flush(&vec);
    std::for_each(vec.begin(), vec.end(), delete_object());
}
END_TEST

START_TEST(test_evs_proto_converge_1by1)
{
    EventLoop el;
    vector<Inst*> vec;
    for (size_t n = 0; n < 8; ++n) {
        vec.resize(n + 1);
        DummyTransport* tp = new DummyTransport();
        vec[n] = new Inst(tp, new EVSProto(&el, tp, UUID(static_cast<int32_t>(n + 1)),
                                           "n" + make_int(n + 1).to_string(), 0));
        vec[n]->ep->shift_to(EVSProto::S_JOINING);
        vec[n]->ep->send_join(n == 0);
        fail_unless(vec[n]->ep->get_state() == (n == 0 ? EVSProto::S_OPERATIONAL: EVSProto::S_JOINING));
        reach_operational(&vec);
        flush(&vec);
    }
    std::for_each(vec.begin(), vec.end(), delete_object());
}
END_TEST

static void send_msgs(vector<Inst*>* vec)
{
    for (vector<Inst*>::iterator i = vec->begin(); i != vec->end();
         ++i) {
        byte_t buf[8] = {'1', '2', '3', '4', '5', '6', '7', '\0'};
        WriteBuf wb(buf, sizeof(buf));
        (*i)->send(&wb);
        stats.acc_sent();
    }
}

static void send_msgs_rnd(vector<Inst*>* vec, size_t max_n)
{
    static uint32_t seq = 0;
    for (vector<Inst*>::iterator i = vec->begin(); i != vec->end();
         ++i) {
        for (size_t n = ::rand() % (max_n + 1); n > 0; --n) {
            byte_t buf[10];
            memset(buf, 0, sizeof(buf));
            sprintf(reinterpret_cast<char*>(buf), "%x", seq);
            WriteBuf wb(buf, sizeof(buf));
            (*i)->send(&wb);
            ++seq;
            stats.acc_sent();
        }
    }
}

static void deliver_msgs(vector<Inst*>* pvec)
{
    bool empty;
    do {
        empty = true;
        for (vector<Inst*>::iterator i = pvec->begin();
             i != pvec->end(); ++i) {
            ReadBuf* rb = (*i)->tp->get_out();
            if (rb) {
                empty = false;
                multicast(pvec, rb, 0);
                rb->release();
            }
        }
    } while (empty == false);
}

static void deliver_msgs_lossy(vector<Inst*>* pvec, int ploss)
{
    bool empty;
    size_t cons_tout_cnt = 0;
    do {
        empty = true;
        for (vector<Inst*>::iterator i = pvec->begin();
             i != pvec->end(); ++i) {
            ReadBuf* rb = (*i)->tp->get_out();
            if (rb) {
                empty = false;
                multicast(pvec, rb, ploss);
                rb->release();
            }
        }
        if (++cons_tout_cnt > 1000)
        {
            for (vector<Inst*>::iterator i = pvec->begin();
                 i != pvec->end(); ++i)
            {
                (*i)->ep->consth->handle();                
                (*i)->ep->ith->handle();
                (*i)->ep->resendth->handle();
            }
            cons_tout_cnt = 0;
        }
    } while (empty == false);
}

START_TEST(test_evs_proto_user_msg)
{
    EventLoop el;
    stats.clear();
    vector<Inst*> vec;
    for (size_t n = 0; n < 8; ++n) {
        vec.resize(n + 1);
        DummyTransport* tp = new DummyTransport();
        vec[n] = new Inst(tp, new EVSProto(&el, tp, UUID(static_cast<int32_t>(n + 1)),
                                           "n" + make_int(n + 1).to_string(), 0));
        vec[n]->ep->shift_to(EVSProto::S_JOINING);
        vec[n]->ep->send_join(n == 0);
        reach_operational(&vec);
        send_msgs(&vec);
        deliver_msgs(&vec);

        send_msgs(&vec);
        send_msgs(&vec);
        deliver_msgs(&vec);
        stats.print();
        stats.clear();
    }


    LOG_INFO("random sending 1");

    for (int i = 0; i < 50; ++i) {
        send_msgs_rnd(&vec, 1);
        deliver_msgs(&vec);
    }

    stats.print();
    stats.clear();


    LOG_INFO("random sending 3");

    for (int i = 0; i < 50; ++i) {
        send_msgs_rnd(&vec, 3);
        deliver_msgs(&vec);
    }

    stats.print();
    stats.clear();


    LOG_INFO("random sending 5");

    for (int i = 0; i < 50; ++i) {
        send_msgs_rnd(&vec, 5);
        deliver_msgs(&vec);
    }

    stats.print();
    stats.clear();


    LOG_INFO("random sending 7");

    for (int i = 0; i < 50; ++i) {
        send_msgs_rnd(&vec, 7);
        deliver_msgs(&vec);
    }

    stats.print();
    stats.clear();

    LOG_INFO("random sending 16");

    for (int i = 0; i < 50; ++i) {
        send_msgs_rnd(&vec, 16);
        deliver_msgs(&vec);
    }

    stats.print();
    stats.clear();

    std::for_each(vec.begin(), vec.end(), delete_object());
}
END_TEST


START_TEST(test_evs_proto_consensus_with_user_msg)
{
    EventLoop el;
    stats.clear();
    vector<Inst*> vec;

    // gu_log_max_level = GU_LOG_DEBUG;
    for (size_t n = 0; n < 8; ++n) {
        log_info << "n = " << n;
        send_msgs_rnd(&vec, 8);
        vec.resize(n + 1);
        DummyTransport* tp = new DummyTransport();
        vec[n] = new Inst(tp, new EVSProto(&el, tp, UUID(static_cast<int32_t>(n + 1)), "n" 
                                           + make_int(n + 1).to_string(), 0));
        vec[n]->ep->shift_to(EVSProto::S_JOINING);
        vec[n]->ep->send_join(n == 0);
        reach_operational(&vec);
        stats.print();
        stats.clear();
    }

    std::for_each(vec.begin(), vec.end(), delete_object());
}
END_TEST

START_TEST(test_evs_proto_msg_loss)
{
    EventLoop el;
    stats.clear();
    vector<Inst*> vec;
    for (size_t n = 0; n < 8; ++n) {
        vec.resize(n + 1);
        DummyTransport* tp = new DummyTransport();
        vec[n] = new Inst(tp, new EVSProto(&el, tp, UUID(static_cast<int32_t>(n + 1)), "n" + 
                                           make_int(n + 1).to_string(), 0));
        vec[n]->ep->shift_to(EVSProto::S_JOINING);
        vec[n]->ep->send_join(false);
    }
    reach_operational(&vec);
    
    for (int i = 0; i < 50; ++i) {
        send_msgs_rnd(&vec, 8);
        deliver_msgs_lossy(&vec, 50);
    }
    flush(&vec);
    stats.print();
    stats.clear();

    std::for_each(vec.begin(), vec.end(), delete_object());
}
END_TEST


START_TEST(test_evs_proto_leave)
{
    EventLoop el;
    stats.clear();
    vector<Inst*> vec;

    for (size_t n = 0; n < 8; ++n) {
        send_msgs_rnd(&vec, 8);
        vec.resize(n + 1);
        DummyTransport* tp = new DummyTransport();
        vec[n] = new Inst(tp, new EVSProto(&el, tp, UUID(static_cast<int32_t>(n + 1)), "n" 
                                           + make_int(n + 1).to_string(), 0));
        vec[n]->ep->shift_to(EVSProto::S_JOINING);
        vec[n]->ep->send_join(n == 0);
        reach_operational(&vec);
        stats.print();
        stats.clear();
    }
    
    for (size_t n = 8; n > 0; --n) {
        send_msgs_rnd(&vec, 8);
        vec[n - 1]->ep->shift_to(EVSProto::S_LEAVING);
        vec[n - 1]->ep->send_leave();
        reach_operational(&vec);
    }

    std::for_each(vec.begin(), vec.end(), delete_object());
}
END_TEST


static void join_inst(EventLoop* el,
                      std::list<vector<Inst* >* >& vlist,
                      vector<Inst*>& vec,
                      size_t *n)
{
    // std::cout << *n << "\n";
    vec.resize(*n + 1);
    DummyTransport* tp = new DummyTransport();
    vec[*n] = new Inst(tp, new EVSProto(el, tp, UUID(static_cast<int32_t>(*n + 1)), "n" 
                                        + make_int(*n + 1).to_string(), 0));
    vec[*n]->ep->shift_to(EVSProto::S_JOINING);
    vec[*n]->ep->send_join(false);

    std::list<vector<Inst* >* >::iterator li;
    if (vlist.size() == 0) {
        vlist.push_back(new vector<Inst*>);
    }
    li = vlist.begin();
    (*li)->push_back(vec[*n]);
    *n = *n + 1;
}

static void leave_inst(vector<Inst*>& vec)
{
    for (vector<Inst*>::iterator i = vec.begin();
         i != vec.end(); ++i) {
        if ((*i)->ep->get_state() == EVSProto::S_OPERATIONAL ||
            (*i)->ep->get_state() == EVSProto::S_RECOVERY) {
            (*i)->ep->shift_to(EVSProto::S_LEAVING);
            (*i)->ep->send_leave();
        }
    }
}


static void split_inst(std::list<vector<Inst* >* >& vlist)
{

}

static void merge_inst(std::list<vector<Inst* >* > *vlist)
{

}

START_TEST(test_evs_proto_full)
{
    EventLoop el;
    size_t n = 0;
    vector<Inst*> vec;
    std::list<vector<Inst* >* > vlist;

    stats.clear();
    // FIXME: This test freezes for some reason after about 500 iterations,
    // increase iterations again when testing with lossy transport.
    for (size_t i = 0; i < 1000; ++i) {
        send_msgs_rnd(&vec, 8);
        if (::rand() % 70 == 0)
            join_inst(&el, vlist, vec, &n);
        if (::rand() % 200 == 0)
            leave_inst(vec);
        if (::rand() % 400 == 0)
            split_inst(vlist);
        else if (::rand() % 50 == 0)
            merge_inst(&vlist);

        send_msgs_rnd(&vec, 8);
        for (std::list<vector<Inst* >* >::iterator li = vlist.begin();
             li != vlist.end(); ++li)
            deliver_msgs_lossy(*li, 50);
    }

    stats.print();
    std::for_each(vec.begin(), vec.end(), delete_object());
    std::for_each(vlist.begin(), vlist.end(), delete_object());
}
END_TEST


class EVSUser : public Toplay, EventContext
{
    Transport* evs;
    EventLoop* el;
    enum State
    {
        CLOSED,
        JOINING,
        S_OPERATIONAL,
        LEAVING,
        LEAVING2
    };
    State state;
    int fd;
    uint64_t recvd;
    uint32_t sent_seq;
    uint32_t delivered_seq;

    EVSUser(const EVSUser&);
    void operator=(const EVSUser&);
public:
    EVSUser(const char* uri_str, EventLoop* el_) :
        evs(0),
        el(el_),
        state(CLOSED),
        fd(-1),
        recvd(0),
        sent_seq(0),
        delivered_seq(0)
    {
        evs = Transport::create(URI(uri_str), el);
        connect(evs, this);
        fd = PseudoFd::alloc_fd();
        el->insert(fd, this);
    }
    
    ~EVSUser()
    {
        check_completeness();
        el->erase(fd);
        PseudoFd::release_fd(fd);
        delete evs;
    }

    void check_completeness()
    {
        fail_unless(sent_seq == delivered_seq, "sent %u delivered %u",
                    sent_seq, delivered_seq);
    }
    
    void start()
    {
        state = JOINING;
        evs->connect();
    }
    
    void stop()
    {
        state = LEAVING;
        evs->close();
        fail_unless(state == CLOSED);
        check_completeness();
    }
    
    void handle_up(const int cid, const ReadBuf* rb, const size_t roff,
                   const ProtoUpMeta* um)
    {
        fail_unless(um != 0);
        if (rb)
        {
            fail_unless(state == S_OPERATIONAL || state == LEAVING ||
                        state == LEAVING2);
            LOG_DEBUG("regular message from " + um->get_source().to_string());
            recvd++;
            fail_unless(um->get_user_type() == 0xab);
            if (um->get_source() == evs->get_uuid())
            {
                IntType<uint32_t> rseq(0xffffffff);
                fail_unless(rseq.read(rb->get_buf(), rb->get_len(), roff) != 0);
                if (rseq.get() != delivered_seq + 1)
                {
                    LOG_ERROR("error in msg sequence: got " 
                              + rseq.to_string() + " expected "
                              + make_int(delivered_seq).to_string());
                }
                fail_unless(rseq.get() == delivered_seq + 1);
                ++delivered_seq;                
            }
        }
        else
        {
            
            fail_unless(um->get_view() != 0);
            LOG_INFO("view message: " + um->get_view()->to_string());
            if (state == JOINING)
            {
                fail_unless(um->get_view()->get_type() == View::V_TRANS);
                fail_unless(um->get_view()->get_members().length() == 1);
                fail_unless(um->get_view()->get_id() == ViewId(evs->get_uuid(), 0));
                state = S_OPERATIONAL;
                el->queue_event(fd, Event(Event::E_USER, Time(Time::now() + Time(0, 50))));
            }
            else if (state == LEAVING)
            {
                if (um->get_view()->get_type() == View::V_TRANS &&
                    um->get_view()->get_members().length() == 1)
                {
                    state = LEAVING2;
                }
            }
            else if (state == LEAVING2)
            {
                fail_unless(um->get_view()->get_type() == View::V_REG);
                fail_unless(um->get_view()->get_id() == ViewId());
                fail_unless(um->get_view()->get_members().length() == 0);
                state = CLOSED;
            }
            else if (um->get_view()->get_type() == View::V_REG)
            {
                EVSProto* p = static_cast<EVS*>(evs)->get_proto();
                fail_unless(p->get_state() == EVSProto::S_OPERATIONAL);
            }
            LOG_INFO("received in prev view: " 
                     + make_int(recvd).to_string());
            recvd = 0;
        }
    }

    void handle_event(const int fd, const Event& ev)
    {
        LOG_TRACE("event, state = " + Int(state).to_string());
        fail_unless(ev.get_cause() == Event::E_USER);
        if (state == S_OPERATIONAL)
        {
            byte_t databuf[8] = "1234567";
            byte_t buf[4];
            WriteBuf wb(databuf, sizeof(databuf));
            if (make_int(sent_seq + 1).write(buf, sizeof(buf), 0) == 0)
            {
                throw FatalException("");
            }
            wb.prepend_hdr(buf, sizeof(buf));
            ProtoDownMeta dm(0xab);
            int ret = pass_down(&wb, &dm);
            wb.rollback_hdr(sizeof(buf));
            if (ret != 0)
            {
                LOG_INFO(string("return: ") + strerror(ret));
            }
            else
            {
                ++sent_seq;
            }
            el->queue_event(fd, Event(Event::E_USER, Time(Time::now() + Time(0, 50000))));
        }
    }

};

START_TEST(test_evs_w_gmcast)
{
    EventLoop el;

    EVSUser u1("gcomm+evs://?gmcast.listen_addr=gcomm+tcp://127.0.0.1:10001&gmcast.group=evs&node.name=n1", &el);
    EVSUser u2("gcomm+evs://127.0.0.1:10001?gmcast.group=evs&gmcast.listen_addr=gcomm+tcp://127.0.0.1:10002&node.name=n2", &el);
    EVSUser u3("gcomm+evs://127.0.0.1:10001?gmcast.group=evs&gmcast.listen_addr=gcomm+tcp://127.0.0.1:10003&node.name=n3", &el);

    u1.start();

    Time stop = Time(Time::now() + Time(5, 0));
    do
    {
        el.poll(100);
    }
    while (Time::now() < stop);

    u2.start();
    stop = Time(Time::now() + Time(5, 0));
    do
    {
        el.poll(100);
    }
    while (Time::now() < stop);

    u3.start();
    stop = Time(Time::now() + Time(5, 0));
    do
    {
        el.poll(100);
    }
    while (Time::now() < stop);

    u1.stop();
    stop = Time(Time::now() + Time(5, 0));
    do
    {
        el.poll(100);
    }
    while (Time::now() < stop);
    u3.stop();

    stop = Time(Time::now() + Time(5, 0));
    do
    {
        el.poll(100);
    }
    while (Time::now() < stop);
    u2.stop();
    // delete poll;
}
END_TEST


bool skip = false;

Suite* evs_suite()
{

    if (::getenv("CHECK_EVS_SKIP"))
        skip = true;

    Suite* s = suite_create("evs");
    TCase* tc;


    tc = tcase_create("test_seqno");
    tcase_add_test(tc, test_seqno);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_msg");
    tcase_add_test(tc, test_msg);
    suite_add_tcase(s, tc);



    tc = tcase_create("test_input_map_basic");
    tcase_add_test(tc, test_input_map_basic);
    suite_add_tcase(s, tc);


    tc = tcase_create("test_input_map_overwrap");
    tcase_add_test(tc, test_input_map_overwrap);
    tcase_set_timeout(tc, 30);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_input_map_random");
    tcase_add_test(tc, test_input_map_random);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_evs_proto_single_boot");
    tcase_add_test(tc, test_evs_proto_single_boot);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_evs_proto_generic_boot");
    tcase_add_test(tc, test_evs_proto_generic_boot);
    tcase_set_timeout(tc, 10);
    suite_add_tcase(s, tc);



    tc = tcase_create("test_evs_proto_double_boot");
    tcase_add_test(tc, test_evs_proto_double_boot);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_evs_proto_user_msg_basic");
    tcase_add_test(tc, test_evs_proto_user_msg_basic);
    suite_add_tcase(s, tc);



    tc = tcase_create("test_evs_proto_leave_basic");
    tcase_add_test(tc, test_evs_proto_leave_basic);
    suite_add_tcase(s, tc);



    tc = tcase_create("test_evs_proto_duplicates");
    tcase_add_test(tc, test_evs_proto_duplicates);
    suite_add_tcase(s, tc);
    

    tc = tcase_create("test_evs_proto_converge");
    tcase_add_test(tc, test_evs_proto_converge);
    tcase_set_timeout(tc, 30);
    suite_add_tcase(s, tc);


    tc = tcase_create("test_evs_proto_converge_1by1");
    tcase_add_test(tc, test_evs_proto_converge_1by1);
    tcase_set_timeout(tc, 30);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_evs_proto_user_msg");
    tcase_add_test(tc, test_evs_proto_user_msg);
    tcase_set_timeout(tc, 30);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_evs_proto_consensus_with_user_msg");
    tcase_add_test(tc, test_evs_proto_consensus_with_user_msg);
    tcase_set_timeout(tc, 10);
    suite_add_tcase(s, tc);

    if (skip)
        return s;

    tc = tcase_create("test_evs_proto_msg_loss");
    tcase_add_test(tc, test_evs_proto_msg_loss);
    tcase_set_timeout(tc, 30);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_evs_proto_leave");
    tcase_add_test(tc, test_evs_proto_leave);
    tcase_set_timeout(tc, 30);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_evs_proto_full");
    tcase_add_test(tc, test_evs_proto_full);
    tcase_set_timeout(tc, 30);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_evs_w_gmcast");
    tcase_add_test(tc, test_evs_w_gmcast);
    tcase_set_timeout(tc, 60);
    suite_add_tcase(s, tc);

    return s;
}
