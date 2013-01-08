
#include <winstd.H>

#include <Profiler.H>
#include <BoxArray.H>
#include <DistributionMapping.H>
#include <ParallelDescriptor.H>
#include <ParmParse.H>
#include <Profiler.H>

#include <iostream>
#include <cstdlib>
#include <list>
#include <map>
#include <vector>
#include <queue>
#include <algorithm>
#include <numeric>

namespace
{
    bool initialized = false;
}

namespace
{
    //
    // Set default values for these in Initialize()!!!
    //
    bool   verbose;
    int    sfc_threshold;
    double max_efficiency;
}

DistributionMapping::Strategy DistributionMapping::m_Strategy;

DistributionMapping::PVMF DistributionMapping::m_BuildMap = 0;

const Array<int>&
DistributionMapping::ProcessorMap () const
{
    return m_ref->m_pmap;
}

DistributionMapping::Strategy
DistributionMapping::strategy ()
{
    return DistributionMapping::m_Strategy;
}

int
DistributionMapping::CacheSize ()
{
    return m_Cache.size();
}

void
DistributionMapping::strategy (DistributionMapping::Strategy how)
{
    DistributionMapping::m_Strategy = how;

    switch (how)
    {
    case ROUNDROBIN:
        m_BuildMap = &DistributionMapping::RoundRobinProcessorMap;
        break;
    case KNAPSACK:
        m_BuildMap = &DistributionMapping::KnapSackProcessorMap;
        break;
    case SFC:
        m_BuildMap = &DistributionMapping::SFCProcessorMap;
        break;
    default:
        BoxLib::Error("Bad DistributionMapping::Strategy");
    }
}

void
DistributionMapping::SFC_Threshold (int n)
{
    sfc_threshold = std::min(n,1);
}

int
DistributionMapping::SFC_Threshold ()
{
    return sfc_threshold;
}

bool
DistributionMapping::operator== (const DistributionMapping& rhs) const
{
    return m_ref == rhs.m_ref || m_ref->m_pmap == rhs.m_ref->m_pmap;
}

bool
DistributionMapping::operator!= (const DistributionMapping& rhs) const
{
    return !operator==(rhs);
}

void
DistributionMapping::Initialize ()
{
    if (initialized) return;
    //
    // Set defaults here!!!
    //
    verbose          = false;
    sfc_threshold    = 0;
    max_efficiency   = 0.9;

    ParmParse pp("DistributionMapping");

    pp.query("v"      ,          verbose);
    pp.query("verbose",          verbose);
    pp.query("efficiency",       max_efficiency);
    pp.query("sfc_threshold",    sfc_threshold);

    std::string theStrategy;

    if (pp.query("strategy", theStrategy))
    {
        if (theStrategy == "ROUNDROBIN")
        {
            strategy(ROUNDROBIN);
        }
        else if (theStrategy == "KNAPSACK")
        {
            strategy(KNAPSACK);
        }
        else if (theStrategy == "SFC")
        {
            strategy(SFC);
        }
        else
        {
            std::string msg("Unknown strategy: ");
            msg += theStrategy;
            BoxLib::Warning(msg.c_str());
        }
    }
    else
    {
        //
        // We default to SFC.
        //
        strategy(SFC);
    }

    BoxLib::ExecOnFinalize(DistributionMapping::Finalize);

    initialized = true;
}

void
DistributionMapping::Finalize ()
{
    initialized = false;

    DistributionMapping::FlushCache();

    DistributionMapping::m_BuildMap = 0;

    DistributionMapping::m_Cache.clear();
}

//
// Our cache of processor maps.
//
std::map< int,LnClassPtr<DistributionMapping::Ref> > DistributionMapping::m_Cache;

void
DistributionMapping::Sort (std::vector<LIpair>& vec,
                           bool                 reverse)
{
    if (vec.size() > 1)
    {
        std::stable_sort(vec.begin(), vec.end(), LIpairComp());

        if (reverse)
        {
            std::reverse(vec.begin(), vec.end());
        }
    }
}

void
DistributionMapping::LeastUsedCPUs (int         nprocs,
                                    Array<int>& result)
{
    result.resize(nprocs);

#ifdef BL_USE_MPI
    BL_PROFILE("DistributionMapping::LeastUsedCPUs()");

    Array<long> bytes(nprocs);

    MPI_Allgather(&BoxLib::total_bytes_allocated_in_fabs,
                  1,
                  ParallelDescriptor::Mpi_typemap<long>::type(),
                  bytes.dataPtr(),
                  1,
                  ParallelDescriptor::Mpi_typemap<long>::type(),
                  ParallelDescriptor::Communicator());

    std::vector<LIpair> LIpairV;

    LIpairV.reserve(nprocs);

    for (int i = 0; i < nprocs; i++)
    {
        LIpairV.push_back(LIpair(bytes[i],i));
    }

    bytes.clear();

    Sort(LIpairV, false);

    for (int i = 0; i < nprocs; i++)
    {
        result[i] = LIpairV[i].second;
    }
#else
    for (int i = 0; i < nprocs; i++)
    {
        result[i] = i;
    }
#endif
}

bool
DistributionMapping::GetMap (const BoxArray& boxes)
{
    const int N = boxes.size();

    BL_ASSERT(m_ref->m_pmap.size() == N + 1);

    std::map< int,LnClassPtr<Ref> >::const_iterator it = m_Cache.find(N+1);

    if (it != m_Cache.end())
    {
        m_ref = it->second;

        BL_ASSERT(m_ref->m_pmap[N] == ParallelDescriptor::MyProc());

        return true;
    }

    return false;
}

DistributionMapping::Ref::Ref () {}

DistributionMapping::DistributionMapping ()
    :
    m_ref(new DistributionMapping::Ref)
{}

DistributionMapping::DistributionMapping (const DistributionMapping& rhs)
    :
    m_ref(rhs.m_ref)
{}

DistributionMapping&
DistributionMapping::operator= (const DistributionMapping& rhs)
{
    m_ref = rhs.m_ref;

    return *this;
}

DistributionMapping::Ref::Ref (const Array<int>& pmap)
    :
    m_pmap(pmap)
{}

DistributionMapping::DistributionMapping (const Array<int>& pmap)
    :
    m_ref(new DistributionMapping::Ref(pmap))
{}

DistributionMapping::Ref::Ref (int len)
    :
    m_pmap(len)
{}

DistributionMapping::DistributionMapping (const BoxArray& boxes, int nprocs)
    :
    m_ref(new DistributionMapping::Ref(boxes.size() + 1))
{
    define(boxes,nprocs);
}

DistributionMapping::Ref::Ref (const Ref& rhs)
    :
    m_pmap(rhs.m_pmap)
{}

DistributionMapping::DistributionMapping (const DistributionMapping& d1,
                                          const DistributionMapping& d2)
    :
    m_ref(new DistributionMapping::Ref(d1.size() + d2.size() - 1))

{
    const Array<int>& pmap_1 = d1.ProcessorMap();
    const Array<int>& pmap_2 = d2.ProcessorMap();

    const int L1 = pmap_1.size() - 1; // Length not including sentinel.
    const int L2 = pmap_2.size() - 1; // Length not including sentinel.

    for (int i = 0; i < L1; i++)
        m_ref->m_pmap[i] = pmap_1[i];

    for (int i = L1, j = 0; j < L2; i++, j++)
        m_ref->m_pmap[i] = pmap_2[j];
    //
    // Set sentinel equal to our processor number.
    //
    m_ref->m_pmap[m_ref->m_pmap.size()-1] = ParallelDescriptor::MyProc();
}

void
DistributionMapping::define (int nboxes, Array<DistributionMapping>& dmaps)
{
    Initialize();
    
    if (m_ref->m_pmap.size() != nboxes + 1)
    {
        m_ref->m_pmap.resize(nboxes + 1);
    }
    
    int N = dmaps.size();
    int i = 0;
    // Loop over supplied Dmaps and add their entries.
    for (int d_i = 0; d_i < N; d_i++)
    {
        const Array<int>& pmap = dmaps[d_i].ProcessorMap();
        const int L = pmap.size()-1;  // Length not including sentinel.
        for (int j = 0; j < L; i++, j++)
            m_ref->m_pmap[i] = pmap[j];
    }
    //
    // Set sentinel equal to our processor number.
    //
    m_ref->m_pmap[m_ref->m_pmap.size()-1] = ParallelDescriptor::MyProc();
}

void
DistributionMapping::define (const BoxArray& boxes, int nprocs)
{
    Initialize();

    if (m_ref->m_pmap.size() != boxes.size() + 1)
    {
        m_ref->m_pmap.resize(boxes.size() + 1);
    }

    if (nprocs == 1)
    {
        for (int i = 0, N = m_ref->m_pmap.size(); i < N; i++)
        {
            m_ref->m_pmap[i] = 0;
        }
    }
    else
    {
        if (!GetMap(boxes))
        {
            BL_ASSERT(m_BuildMap != 0);

            (this->*m_BuildMap)(boxes,nprocs);
            //
            // Add the new processor map to the cache.
            //
            m_Cache.insert(std::make_pair(m_ref->m_pmap.size(),m_ref));
        }
    }
}

DistributionMapping::~DistributionMapping () {}

void
DistributionMapping::FlushCache ()
{
    CacheStats(std::cout);
    //
    // Remove maps that aren't referenced anywhere else.
    //
    std::map< int,LnClassPtr<Ref> >::iterator it = m_Cache.begin();

    while (it != m_Cache.end())
    {
        if (it->second.linkCount() == 1)
        {
            m_Cache.erase(it++);
        }
        else
        {
            ++it;
        }
    }
}

void
DistributionMapping::RoundRobinDoIt (int                  nboxes,
                                     int                  nprocs,
                                     std::vector<LIpair>* LIpairV)
{
    Array<int> ord;

    LeastUsedCPUs(nprocs,ord);

    if (LIpairV)
    {
        BL_ASSERT(LIpairV->size() == nboxes);

        for (int i = 0; i < nboxes; i++)
        {
            m_ref->m_pmap[(*LIpairV)[i].second] = ord[i%nprocs];
        }
    }
    else
    {
        for (int i = 0; i < nboxes; i++)
        {
            m_ref->m_pmap[i] = ord[i%nprocs];
        }
    }
    //
    // Set sentinel equal to our processor number.
    //
    m_ref->m_pmap[nboxes] = ParallelDescriptor::MyProc();
}

void
DistributionMapping::RoundRobinProcessorMap (int nboxes, int nprocs)
{
    BL_ASSERT(nboxes > 0);

    if (m_ref->m_pmap.size() != nboxes + 1)
    {
        m_ref->m_pmap.resize(nboxes + 1);
    }

    RoundRobinDoIt(nboxes, nprocs);
}

void
DistributionMapping::RoundRobinProcessorMap (const BoxArray& boxes, int nprocs)
{
    BL_ASSERT(boxes.size() > 0);
    BL_ASSERT(m_ref->m_pmap.size() == boxes.size() + 1);
    //
    // Create ordering of boxes from largest to smallest.
    // When we round-robin the boxes we want to go from largest
    // to smallest box, starting from the CPU having the least
    // amount of FAB data to the one having the most.  This "should"
    // help even out the FAB data distribution when running on large
    // numbers of CPUs, where the lower levels of the calculation are
    // using RoundRobin to lay out fewer than NProc boxes across
    // the CPUs.
    //
    std::vector<LIpair> LIpairV;

    LIpairV.reserve(boxes.size());

    for (int i = 0, N = boxes.size(); i < N; i++)
    {
        LIpairV.push_back(LIpair(boxes[i].numPts(),i));
    }

    Sort(LIpairV, true);

    RoundRobinDoIt(boxes.size(), nprocs, &LIpairV);
}

class WeightedBox
{
    int  m_boxid;
    long m_weight;
public:
    WeightedBox () {}
    WeightedBox (int b, int w) : m_boxid(b), m_weight(w) {}
    long weight () const { return m_weight; }
    int  boxid ()  const { return m_boxid;  }

    bool operator< (const WeightedBox& rhs) const
    {
        return weight() > rhs.weight();
    }
};

class WeightedBoxList
{
    std::list<WeightedBox>* m_lb;
    long                    m_weight;
public:
    WeightedBoxList (std::list<WeightedBox>* lb) : m_lb(lb), m_weight(0) {}
    long weight () const
    {
        return m_weight;
    }
    void erase (std::list<WeightedBox>::iterator& it)
    {
        m_weight -= it->weight();
        m_lb->erase(it);
    }
    void push_back (const WeightedBox& bx)
    {
        m_weight += bx.weight();
        m_lb->push_back(bx);
    }
    int size () const { return m_lb->size(); }
    std::list<WeightedBox>::const_iterator begin () const { return m_lb->begin(); }
    std::list<WeightedBox>::iterator begin ()             { return m_lb->begin(); }
    std::list<WeightedBox>::const_iterator end () const   { return m_lb->end();   }
    std::list<WeightedBox>::iterator end ()               { return m_lb->end();   }

    bool operator< (const WeightedBoxList& rhs) const
    {
        return weight() > rhs.weight();
    }
};

static
void
knapsack (const std::vector<long>&         wgts,
          int                              nprocs,
          std::vector< std::vector<int> >& result,
          double&                          efficiency,
          bool                             do_full_knapsack)
{
    //
    // Sort balls by size largest first.
    //
    result.resize(nprocs);

    std::vector<WeightedBox> lb;
    lb.reserve(wgts.size());
    for (unsigned int i = 0, N = wgts.size(); i < N; ++i)
    {
        lb.push_back(WeightedBox(i, wgts[i]));
    }
    BL_ASSERT(lb.size() == wgts.size());
    std::sort(lb.begin(), lb.end());
    BL_ASSERT(lb.size() == wgts.size());
    //
    // For each ball, starting with heaviest, assign ball to the lightest box.
    //
    std::priority_queue<WeightedBoxList>   wblq;
    std::vector< std::list<WeightedBox>* > vbbs(nprocs);
    for (int i  = 0; i < nprocs; ++i)
    {
        vbbs[i] = new std::list<WeightedBox>;
        wblq.push(WeightedBoxList(vbbs[i]));
    }
    BL_ASSERT(int(wblq.size()) == nprocs);
    for (unsigned int i = 0, N = wgts.size(); i < N; ++i)
    {
        WeightedBoxList wbl = wblq.top();
        wblq.pop();
        wbl.push_back(lb[i]);
        wblq.push(wbl);
    }
    BL_ASSERT(int(wblq.size()) == nprocs);
    std::list<WeightedBoxList> wblqg;
    while (!wblq.empty())
    {
        wblqg.push_back(wblq.top());
        wblq.pop();
    }
    BL_ASSERT(int(wblqg.size()) == nprocs);
    wblqg.sort();
    //
    // Compute the max weight and the sum of the weights.
    //
    double max_weight = 0;
    double sum_weight = 0;
    std::list<WeightedBoxList>::iterator it = wblqg.begin();
    for (std::list<WeightedBoxList>::const_iterator End =  wblqg.end(); it != End; ++it)
    {
        long wgt = (*it).weight();
        sum_weight += wgt;
        max_weight = (wgt > max_weight) ? wgt : max_weight;
    }

    efficiency = sum_weight/(nprocs*max_weight);

top:

    std::list<WeightedBoxList>::iterator it_top = wblqg.begin();

    WeightedBoxList wbl_top = *it_top;
    //
    // For each ball in the heaviest box.
    //
    std::list<WeightedBox>::iterator it_wb = wbl_top.begin();

    if (efficiency > max_efficiency || !do_full_knapsack) goto bottom;

    for ( ; it_wb != wbl_top.end(); ++it_wb )
    {
        //
        // For each ball not in the heaviest box.
        //
        std::list<WeightedBoxList>::iterator it_chk = it_top;
        it_chk++;
        for ( ; it_chk != wblqg.end(); ++it_chk)
        {
            WeightedBoxList wbl_chk = *it_chk;
            std::list<WeightedBox>::iterator it_owb = wbl_chk.begin();
            for ( ; it_owb != wbl_chk.end(); ++it_owb)
            {
                //
                // If exchanging these two balls reduces the load balance,
                // then exchange them and go to top.  The way we are doing
                // things, sum_weight cannot change.  So the efficiency will
                // increase if after we switch the two balls *it_wb and
                // *it_owb the max weight is reduced.
                //
                double w_tb = (*it_top).weight() + (*it_owb).weight() - (*it_wb).weight();
                double w_ob = (*it_chk).weight() + (*it_wb).weight() - (*it_owb).weight();
                //
                // If the other ball reduces the weight of the top box when
                // swapped, then it will change the efficiency.
                //
                if (w_tb < (*it_top).weight() && w_ob < (*it_top).weight())
                {
                    //
                    // Adjust the sum weight and the max weight.
                    //
                    WeightedBox wb = *it_wb;
                    WeightedBox owb = *it_owb;
                    wblqg.erase(it_top);
                    wblqg.erase(it_chk);
                    wbl_top.erase(it_wb);
                    wbl_chk.erase(it_owb);
                    wbl_top.push_back(owb);
                    wbl_chk.push_back(wb);
                    std::list<WeightedBoxList> tmp;
                    tmp.push_back(wbl_top);
                    tmp.push_back(wbl_chk);
                    tmp.sort();
                    wblqg.merge(tmp);
                    max_weight = (*wblqg.begin()).weight();
                    efficiency = sum_weight/(nprocs*max_weight);
                    goto top;
                }
            }
        }
    }

 bottom:
    //
    // Here I am "load-balanced".
    //
    std::list<WeightedBoxList>::const_iterator cit = wblqg.begin();

    for (int i = 0; i < nprocs; ++i)
    {
        const WeightedBoxList& wbl = *cit;

        result[i].reserve(wbl.size());

        for (std::list<WeightedBox>::const_iterator it1 = wbl.begin(), End = wbl.end();
            it1 != End;
              ++it1)
        {
            result[i].push_back((*it1).boxid());
        }
        ++cit;
    }

    for (int i  = 0; i < nprocs; i++)
        delete vbbs[i];
}

void
DistributionMapping::KnapSackDoIt (const std::vector<long>& wgts,
                                   int                      nprocs,
                                   double&                  efficiency,
                                   bool                     do_full_knapsack)
{
    BL_PROFILE("DistributionMapping::KnapSackDoIt()");

    std::vector< std::vector<int> > vec;

    efficiency = 0;

    knapsack(wgts,nprocs,vec,efficiency,do_full_knapsack);

    BL_ASSERT(vec.size() == nprocs);

    Array<long> wgts_per_cpu(nprocs,0);

    for (unsigned int i = 0, N = vec.size(); i < N; i++)
    {
        for (std::vector<int>::iterator lit = vec[i].begin(), End = vec[i].end();
             lit != End;
             ++lit)
        {
            wgts_per_cpu[i] += wgts[*lit];
        }
    }

    std::vector<LIpair> LIpairV;

    LIpairV.reserve(nprocs);

    for (int i = 0; i < nprocs; i++)
    {
        LIpairV.push_back(LIpair(wgts_per_cpu[i],i));
    }

    Sort(LIpairV, true);

    Array<int> ord;

    LeastUsedCPUs(nprocs,ord);

    for (unsigned int i = 0, N = vec.size(); i < N; i++)
    {
        const int idx = LIpairV[i].second;
        const int cpu = ord[i];

        for (std::vector<int>::iterator lit = vec[idx].begin(), End = vec[idx].end();
             lit != End;
             ++lit)
        {
            m_ref->m_pmap[*lit] = cpu;
        }
    }
    //
    // Set sentinel equal to our processor number.
    //
    m_ref->m_pmap[wgts.size()] = ParallelDescriptor::MyProc();

    if (verbose && ParallelDescriptor::IOProcessor())
    {
        std::cout << "KNAPSACK efficiency: " << efficiency << '\n';
    }
}

void
DistributionMapping::KnapSackProcessorMap (const std::vector<long>& wgts,
                                           int                      nprocs,
                                           double*                  efficiency,
                                           bool                     do_full_knapsack)
{
    BL_ASSERT(wgts.size() > 0);

    if (m_ref->m_pmap.size() !=  wgts.size() + 1)
    {
        m_ref->m_pmap.resize(wgts.size() + 1);
    }

    if (wgts.size() <= nprocs || nprocs < 2)
    {
        RoundRobinProcessorMap(wgts.size(),nprocs);

        if (efficiency) *efficiency = 1;
    }
    else
    {
        double eff = 0;
        KnapSackDoIt(wgts, nprocs, eff, do_full_knapsack);
        if (efficiency) *efficiency = eff;
    }
}

void
DistributionMapping::KnapSackProcessorMap (const BoxArray& boxes,
					   int             nprocs)
{
    BL_ASSERT(boxes.size() > 0);
    BL_ASSERT(m_ref->m_pmap.size() == boxes.size()+1);

    if (boxes.size() <= nprocs || nprocs < 2)
    {
        RoundRobinProcessorMap(boxes,nprocs);
    }
    else
    {
        std::vector<long> wgts(boxes.size());

        for (unsigned int i = 0, N = boxes.size(); i < N; i++)
            wgts[i] = boxes[i].numPts();

        double effi = 0;
        bool do_full_knapsack = true;
        KnapSackDoIt(wgts, nprocs, effi, do_full_knapsack);
    }
}

namespace
{
    struct SFCToken
    {
        class Compare
        {
        public:
            bool operator () (const SFCToken& lhs,
                              const SFCToken& rhs) const;
        };

        SFCToken (int box, const IntVect& idx, Real vol)
            :
            m_box(box), m_idx(idx), m_vol(vol) {}

        int     m_box;
        IntVect m_idx;
        Real    m_vol;

        static int MaxPower;
    };
}

int SFCToken::MaxPower = 64;

bool
SFCToken::Compare::operator () (const SFCToken& lhs,
                                const SFCToken& rhs) const
{
    for (int i = SFCToken::MaxPower - 1; i >= 0; --i)
    {
        const int N = (1<<i);

        for (int j = BL_SPACEDIM-1; j >= 0; --j)
        {
            const int il = lhs.m_idx[j]/N;
            const int ir = rhs.m_idx[j]/N;

            if (il < ir)
            {
                return true;
            }
            else if (il > ir)
            {
                return false;
            }
        }
    }

    return false;
}

static
void
Distribute (const std::vector<SFCToken>&     tokens,
            int                              nprocs,
            Real                             volpercpu,
            std::vector< std::vector<int> >& v)

{
    BL_ASSERT(v.size() == nprocs);

    int  K        = 0;
    Real totalvol = 0;

    const int Navg = tokens.size() / nprocs;

    for (int i = 0; i < nprocs; i++)
    {
        int  cnt = 0;
        Real vol = 0;

        v[i].reserve(Navg + 2);

        for ( int TSZ = tokens.size();
              K < TSZ && (i == (nprocs-1) || vol < volpercpu);
              cnt++, K++)
        {
            vol += tokens[K].m_vol;

            v[i].push_back(tokens[K].m_box);
        }

        totalvol += vol;

        if ((totalvol/(i+1)) > volpercpu &&
            cnt > 1                      &&
            K < tokens.size())
        {
            K--;
            v[i].pop_back();
            totalvol -= tokens[K].m_vol;;
        }
    }

#ifndef NDEBUG
    int cnt = 0;
    for (int i = 0; i < nprocs; i++)
        cnt += v[i].size();
    BL_ASSERT(cnt == tokens.size());
#endif
}

void
DistributionMapping::SFCProcessorMapDoIt (const BoxArray&          boxes,
                                          const std::vector<long>& wgts,
                                          int                      nprocs)
{
    BL_PROFILE("DistributionMapping::SFCProcessorMapDoIt()");

    std::vector<SFCToken> tokens;

    tokens.reserve(boxes.size());

    int maxijk = 0;

    for (int i = 0, N = boxes.size(); i < N; i++)
    {
        tokens.push_back(SFCToken(i,boxes[i].smallEnd(),wgts[i]));

        const SFCToken& token = tokens.back();

        D_TERM(maxijk = std::max(maxijk, token.m_idx[0]);,
               maxijk = std::max(maxijk, token.m_idx[1]);,
               maxijk = std::max(maxijk, token.m_idx[2]););
    }
    //
    // Set SFCToken::MaxPower for BoxArray.
    //
    int m = 0;
    for ( ; (1<<m) <= maxijk; m++)
        ;
    SFCToken::MaxPower = m;
    //
    // Put'm in Morton space filling curve order.
    //
    std::sort(tokens.begin(), tokens.end(), SFCToken::Compare());
    //
    // Split'm up as equitably as possible per CPU.
    //
    Real volpercpu = 0;
    for (int i = 0, N = tokens.size(); i < N; i++)
        volpercpu += tokens[i].m_vol;
    volpercpu /= nprocs;

    std::vector< std::vector<int> > vec(nprocs);

    Distribute(tokens,nprocs,volpercpu,vec);

    tokens.clear();

    Array<long> wgts_per_cpu(nprocs,0);

    for (unsigned int i = 0, N = vec.size(); i < N; i++)
    {
        const std::vector<int>& vi = vec[i];

        for (int j = 0, M = vi.size(); j < M; j++)
            wgts_per_cpu[i] += wgts[vi[j]];
    }

    std::vector<LIpair> LIpairV;

    LIpairV.reserve(nprocs);

    for (int i = 0; i < nprocs; i++)
    {
        LIpairV.push_back(LIpair(wgts_per_cpu[i],i));
    }

    Sort(LIpairV, true);

    Array<int> ord;

    LeastUsedCPUs(nprocs,ord);

    for (int i = 0; i < nprocs; i++)
    {
        const int cpu = ord[i];
        const int idx = LIpairV[i].second;

        const std::vector<int>& vi = vec[idx];

        for (int j = 0, N = vi.size(); j < N; j++)
        {
            m_ref->m_pmap[vi[j]] = cpu;
        }
    }
    //
    // Set sentinel equal to our processor number.
    //
    m_ref->m_pmap[boxes.size()] = ParallelDescriptor::MyProc();

    if (verbose && ParallelDescriptor::IOProcessor())
    {
        Real sum_wgt = 0, max_wgt = 0;
        for (int i = 0, N = wgts_per_cpu.size(); i < N; i++)
        {
            const long W = wgts_per_cpu[i];
            if (W > max_wgt)
                max_wgt = W;
            sum_wgt += W;
        }

        std::cout << "SFC efficiency: " << (sum_wgt/(nprocs*max_wgt)) << '\n';
    }
}

void
DistributionMapping::SFCProcessorMap (const BoxArray& boxes,
                                      int             nprocs)
{
    BL_ASSERT(boxes.size() > 0);

    if (m_ref->m_pmap.size() != boxes.size() + 1)
    {
        m_ref->m_pmap.resize(boxes.size()+1);
    }

    if (boxes.size() < sfc_threshold*nprocs)
    {
        KnapSackProcessorMap(boxes,nprocs);
    }
    else
    {
        std::vector<long> wgts;

        wgts.reserve(boxes.size());

        for (BoxArray::const_iterator it = boxes.begin(), End = boxes.end(); it != End; ++it)
        {
            wgts.push_back(it->volume());
        }

        SFCProcessorMapDoIt(boxes,wgts,nprocs);
    }
}

void
DistributionMapping::SFCProcessorMap (const BoxArray&          boxes,
                                      const std::vector<long>& wgts,
                                      int                      nprocs)
{
    BL_ASSERT(boxes.size() > 0);
    BL_ASSERT(boxes.size() == wgts.size());

    if (m_ref->m_pmap.size() != wgts.size() + 1)
    {
        m_ref->m_pmap.resize(wgts.size()+1);
    }

    if (boxes.size() < sfc_threshold*nprocs)
    {
        KnapSackProcessorMap(wgts,nprocs);
    }
    else
    {
        SFCProcessorMapDoIt(boxes,wgts,nprocs);
    }
}

void
DistributionMapping::CacheStats (std::ostream& os)
{
    if (ParallelDescriptor::IOProcessor() && m_Cache.size())
    {
        os << "DistributionMapping::m_Cache.size() = "
           << m_Cache.size()
           << " [ (refs,size): ";

        for (std::map< int,LnClassPtr<Ref> >::const_iterator it = m_Cache.begin();
             it != m_Cache.end();
             ++it)
        {
            os << '(' << it->second.linkCount() << ',' << it->second->m_pmap.size()-1 << ") ";
        }

        os << "]\n";
    }
}

std::ostream&
operator<< (std::ostream&              os,
            const DistributionMapping& pmap)
{
    os << "(DistributionMapping" << '\n';
    //
    // Do not print the sentinel value.
    //
    for (int i = 0; i < pmap.ProcessorMap().size() - 1; i++)
    {
        os << "m_pmap[" << i << "] = " << pmap.ProcessorMap()[i] << '\n';
    }

    os << ')' << '\n';

    if (os.fail())
        BoxLib::Error("operator<<(ostream &, DistributionMapping &) failed");

    return os;
}
