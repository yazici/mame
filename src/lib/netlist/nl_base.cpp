// license:GPL-2.0+
// copyright-holders:Couriersud
/*
 * nlbase.c
 *
 */

#include "solver/nld_matrix_solver.h"

#include "plib/palloc.h"

#include "nl_base.h"
#include "devices/nlid_system.h"
#include "nl_util.h"

const netlist::netlist_time netlist::netlist_time::zero = netlist::netlist_time(0);

namespace netlist
{

#if (NL_USE_MEMPOOL)
static plib::pmempool p(65536, 16);

void * object_t::operator new (size_t size)
{
	return p.alloc(size);
}

void object_t::operator delete (void * mem)
{
    if (mem)
    	p.free(mem);
}
#else
void * object_t::operator new (size_t size)
{
	return ::operator new(size);
}

void object_t::operator delete (void * mem)
{
    if (mem)
    	::operator delete(mem);
}
#endif

// ----------------------------------------------------------------------------------------
// logic_family_ttl_t
// ----------------------------------------------------------------------------------------

class logic_family_ttl_t : public logic_family_desc_t
{
public:
	logic_family_ttl_t() : logic_family_desc_t()
	{
		m_low_thresh_V = 0.8;
		m_high_thresh_V = 2.0;
		// m_low_V  - these depend on sinked/sourced current. Values should be suitable for typical applications.
		m_low_V = 0.1;
		m_high_V = 4.0;
		m_R_low = 1.0;
		m_R_high = 130.0;
	}
	virtual plib::owned_ptr<devices::nld_base_d_to_a_proxy> create_d_a_proxy(netlist_t &anetlist, const pstring &name, logic_output_t *proxied) const override
	{
		return plib::owned_ptr<devices::nld_base_d_to_a_proxy>::Create<devices::nld_d_to_a_proxy>(anetlist, name, proxied);
	}
};

//FIXME: set to proper values
class logic_family_cd4xxx_t : public logic_family_desc_t
{
public:
	logic_family_cd4xxx_t() : logic_family_desc_t()
	{
		m_low_thresh_V = 0.8;
		m_high_thresh_V = 2.0;
		// m_low_V  - these depend on sinked/sourced current. Values should be suitable for typical applications.
		m_low_V = 0.05;
		m_high_V = 4.95;
		m_R_low = 10.0;
		m_R_high = 10.0;
	}
	virtual plib::owned_ptr<devices::nld_base_d_to_a_proxy> create_d_a_proxy(netlist_t &anetlist, const pstring &name, logic_output_t *proxied) const override
	{
		return plib::owned_ptr<devices::nld_base_d_to_a_proxy>::Create<devices::nld_d_to_a_proxy>(anetlist, name, proxied);
	}
};

const logic_family_desc_t *family_TTL()
{
	static logic_family_ttl_t obj;
	return &obj;
}
const logic_family_desc_t *family_CD4XXX()
{
	static logic_family_cd4xxx_t obj;
	return &obj;
}

// ----------------------------------------------------------------------------------------
// queue_t
// ----------------------------------------------------------------------------------------

queue_t::queue_t(netlist_t &nl)
	: timed_queue<net_t *, netlist_time>(512)
	, object_t(nl, "QUEUE", QUEUE)
	, plib::pstate_callback_t()
	, m_qsize(0)
	, m_times(512)
	, m_names(512)
{
}

void queue_t::register_state(plib::pstate_manager_t &manager, const pstring &module)
{
	netlist().log().debug("register_state\n");
	manager.save_item(m_qsize, this, module + "." + "qsize");
	manager.save_item(&m_times[0], this, module + "." + "times", m_times.size());
	manager.save_item(&(m_names[0].m_buf[0]), this, module + "." + "names", m_names.size() * sizeof(names_t));
}

void queue_t::on_pre_save()
{
	netlist().log().debug("on_pre_save\n");
	m_qsize = this->count();
	netlist().log().debug("current time {1} qsize {2}\n", netlist().time().as_double(), m_qsize);
	for (int i = 0; i < m_qsize; i++ )
	{
		m_times[i] =  this->listptr()[i].exec_time().as_raw();
		pstring p = this->listptr()[i].object()->name();
		int n = p.len();
		n = std::min(63, n);
		std::strncpy(m_names[i].m_buf, p.cstr(), n);
		m_names[i].m_buf[n] = 0;
	}
}


void queue_t::on_post_load()
{
	this->clear();
	netlist().log().debug("current time {1} qsize {2}\n", netlist().time().as_double(), m_qsize);
	for (int i = 0; i < m_qsize; i++ )
	{
		net_t *n = netlist().find_net(m_names[i].m_buf);
		//log().debug("Got {1} ==> {2}\n", qtemp[i].m_name, n));
		//log().debug("schedule time {1} ({2})\n", n->time().as_double(),  netlist_time::from_raw(m_times[i]).as_double()));
		this->push(queue_t::entry_t(netlist_time::from_raw(m_times[i]), n));
	}
}

// ----------------------------------------------------------------------------------------
// object_t
// ----------------------------------------------------------------------------------------

ATTR_COLD object_t::object_t(const type_t atype)
: m_objtype(atype)
, m_netlist(nullptr)
{}

ATTR_COLD object_t::object_t(netlist_t &nl, const type_t atype)
: m_objtype(atype)
, m_netlist(&nl)
{}

ATTR_COLD object_t::object_t(netlist_t &nl, const pstring &aname, const type_t atype)
: m_name(aname)
, m_objtype(atype)
, m_netlist(&nl)
{
	//printf("reg %s\n", this->name().cstr());
	save_register();
}

ATTR_COLD object_t::~object_t()
{
}

ATTR_COLD void object_t::init_object(netlist_t &nl, const pstring &aname)
{
	m_netlist = &nl;
	m_name = aname;
	save_register();
}

ATTR_COLD const pstring &object_t::name() const
{
	if (m_name == "")
		netlist().log().fatal("object not initialized");
	return m_name;
}

// ----------------------------------------------------------------------------------------
// device_object_t
// ----------------------------------------------------------------------------------------

ATTR_COLD device_object_t::device_object_t(const type_t atype)
: object_t(atype)
, m_device(nullptr)
{
}

ATTR_COLD device_object_t::device_object_t(core_device_t &dev, const type_t atype)
: object_t(dev.netlist(), atype)
, m_device(&dev)
{
}

ATTR_COLD device_object_t::device_object_t(core_device_t &dev, const pstring &aname, const type_t atype)
: object_t(dev.netlist(), aname, atype)
, m_device(&dev)
{
}


ATTR_COLD void device_object_t::init_object(core_device_t &dev,
		const pstring &aname)
{
	object_t::init_object(dev.netlist(), aname);
	m_device = &dev;
}

// ----------------------------------------------------------------------------------------
// netlist_t
// ----------------------------------------------------------------------------------------

netlist_t::netlist_t(const pstring &aname)
	:   pstate_manager_t(),
		m_stop(netlist_time::zero),
		m_time(netlist_time::zero),
		m_use_deactivate(0),
		m_queue(*this),
		m_mainclock(nullptr),
		m_solver(nullptr),
		m_gnd(nullptr),
		m_params(nullptr),
		m_name(aname),
		m_setup(nullptr),
		m_log(this),
		m_lib(nullptr)
{
	save_item(static_cast<plib::pstate_callback_t &>(m_queue), this,  "m_queue");
	save_item(m_time, this, "m_time");
}

netlist_t::~netlist_t()
{

	m_nets.clear();
	m_devices.clear();

	pfree(m_lib);
	pstring::resetmem();
}

ATTR_HOT nl_double netlist_t::gmin() const
{
	return solver()->gmin();
}

ATTR_COLD void netlist_t::start()
{
	/* load the library ... */

	pstring libpath = nl_util::environment("NL_BOOSTLIB", nl_util::buildpath({".", "nlboost.so"}));

	m_lib = plib::palloc<plib::dynlib>(libpath);

	/* make sure the solver and parameters are started first! */

	for (auto & e : setup().m_device_factory)
	{
		if ( setup().factory().is_class<devices::NETLIB_NAME(mainclock)>(e.second)
				|| setup().factory().is_class<devices::NETLIB_NAME(solver)>(e.second)
				|| setup().factory().is_class<devices::NETLIB_NAME(gnd)>(e.second)
				|| setup().factory().is_class<devices::NETLIB_NAME(netlistparams)>(e.second))
		{
			auto dev = plib::owned_ptr<device_t>(e.second->Create(*this, e.first));
			setup().register_dev_s(std::move(dev));
		}
	}

	log().debug("Searching for mainclock and solver ...\n");

	m_mainclock = get_single_device<devices::NETLIB_NAME(mainclock)>("mainclock");
	m_solver = get_single_device<devices::NETLIB_NAME(solver)>("solver");
	m_gnd = get_single_device<devices::NETLIB_NAME(gnd)>("gnd");
	m_params = get_single_device<devices::NETLIB_NAME(netlistparams)>("parameter");

	m_use_deactivate = (m_params->m_use_deactivate.Value() ? true : false);

	/* create devices */

	for (auto & e : setup().m_device_factory)
	{
		if ( !setup().factory().is_class<devices::NETLIB_NAME(mainclock)>(e.second)
				&& !setup().factory().is_class<devices::NETLIB_NAME(solver)>(e.second)
				&& !setup().factory().is_class<devices::NETLIB_NAME(gnd)>(e.second)
				&& !setup().factory().is_class<devices::NETLIB_NAME(netlistparams)>(e.second))
		{
			auto dev = plib::owned_ptr<device_t>(e.second->Create(*this, e.first));
			setup().register_dev_s(std::move(dev));
		}
	}

}

ATTR_COLD void netlist_t::stop()
{
	/* find the main clock and solver ... */

	log().debug("Stopping all devices ...\n");
	for (auto & dev : m_devices)
		dev->stop_dev();
}

ATTR_COLD net_t *netlist_t::find_net(const pstring &name)
{
	for (auto & net : m_nets)
		if (net->name() == name)
			return net.get();

	return nullptr;
}

ATTR_COLD void netlist_t::rebuild_lists()
{
	for (auto & net : m_nets)
		net->rebuild_list();
}


ATTR_COLD void netlist_t::reset()
{
	m_time = netlist_time::zero;
	m_queue.clear();
	if (m_mainclock != nullptr)
		m_mainclock->m_Q.net().set_time(netlist_time::zero);
	//if (m_solver != nullptr)
	//	m_solver->do_reset();

	// Reset all nets once !
	for (std::size_t i = 0; i < m_nets.size(); i++)
		m_nets[i]->do_reset();

	// Reset all devices once !
	for (auto & dev : m_devices)
		dev->do_reset();

	// Make sure everything depending on parameters is set
	for (auto & dev : m_devices)
		dev->update_param();

	// Step all devices once !
#if 0
	for (std::size_t i = 0; i < m_devices.size(); i++)
	{
		m_devices[i]->update_dev();
	}
#else
	/* FIXME: this makes breakout attract mode working again.
	 * It is however not acceptable that this depends on the startup order.
	 * Best would be, if reset would call update_dev for devices which need it.
	 */
	for (int i = m_devices.size() - 1; i >= 0; i--)
		m_devices[i]->update_dev();
#endif
}


ATTR_HOT void netlist_t::process_queue(const netlist_time &delta)
{
	m_stop = m_time + delta;

	if (m_mainclock == nullptr)
	{
		while ( (m_time < m_stop) && (m_queue.is_not_empty()))
		{
			const queue_t::entry_t &e = m_queue.pop();
			m_time = e.exec_time();
			e.object()->update_devs();

			add_to_stat(m_perf_out_processed, 1);
		}
		if (m_queue.is_empty())
			m_time = m_stop;

	} else {
		logic_net_t &mc_net = m_mainclock->m_Q.net().as_logic();
		const netlist_time inc = m_mainclock->m_inc;
		netlist_time mc_time(mc_net.time());

		while (m_time < m_stop)
		{
			if (m_queue.is_not_empty())
			{
				while (m_queue.top().exec_time() > mc_time)
				{
					m_time = mc_time;
					mc_time += inc;
					mc_net.toggle_new_Q();
					mc_net.update_devs();
					//devices::NETLIB_NAME(mainclock)::mc_update(mc_net);
				}

				const queue_t::entry_t &e = m_queue.pop();
				m_time = e.exec_time();
				e.object()->update_devs();

			} else {
				m_time = mc_time;
				mc_time += inc;
				mc_net.toggle_new_Q();
				mc_net.update_devs();
				//devices::NETLIB_NAME(mainclock)::mc_update(mc_net);
			}

			add_to_stat(m_perf_out_processed, 1);
		}
		mc_net.set_time(mc_time);
	}
}

void netlist_t::print_stats() const
{
#if (NL_KEEP_STATISTICS)
	{
		for (std::size_t i = 0; i < m_devices.size(); i++)
		{
			core_device_t *entry = m_devices[i].get();
			printf("Device %20s : %12d %12d %15ld\n", entry->name().cstr(), entry->stat_call_count, entry->stat_update_count, (long int) entry->stat_total_time / (entry->stat_update_count + 1));
		}
		printf("Queue Pushes %15d\n", queue().m_prof_call);
		printf("Queue Moves  %15d\n", queue().m_prof_sortmove);
	}
#endif
}

// ----------------------------------------------------------------------------------------
// Parameters ...
// ----------------------------------------------------------------------------------------

template <typename C, param_t::param_type_t T>
param_template_t<C, T>::param_template_t(device_t &device, const pstring name, const C val)
: param_t(T, device, device.name() + "." + name)
, m_param(val)
{
	/* pstrings not yet supported, these need special logic */
	if (T != param_t::STRING && T != param_t::MODEL)
		save(NLNAME(m_param));
	device.setup().register_and_set_param(device.name() + "." + name, *this);
}

template class param_template_t<double, param_t::DOUBLE>;
template class param_template_t<int, param_t::INTEGER>;
template class param_template_t<int, param_t::LOGIC>;
template class param_template_t<pstring, param_t::STRING>;
template class param_template_t<pstring, param_t::MODEL>;

#if 0
template <class C, class T>
ATTR_COLD void device_t::register_param(const pstring &sname, C &param, const T initialVal)
{
	pstring fullname = this->name() + "." + sname;
	param.init_object(*this, fullname);
	param.initial(initialVal);
	setup().register_object(*this, fullname, param);
}

template ATTR_COLD void device_t::register_param(const pstring &sname, param_double_t &param, const double initialVal);
template ATTR_COLD void device_t::register_param(const pstring &sname, param_double_t &param, const float initialVal);
template ATTR_COLD void device_t::register_param(const pstring &sname, param_int_t &param, const int initialVal);
template ATTR_COLD void device_t::register_param(const pstring &sname, param_logic_t &param, const int initialVal);
template ATTR_COLD void device_t::register_param(const pstring &sname, param_str_t &param, const char * const initialVal);
template ATTR_COLD void device_t::register_param(const pstring &sname, param_str_t &param, const pstring &initialVal);
template ATTR_COLD void device_t::register_param(const pstring &sname, param_model_t &param, const char * const initialVal);
#endif


// ----------------------------------------------------------------------------------------
// core_device_t
// ----------------------------------------------------------------------------------------

ATTR_COLD core_device_t::core_device_t(netlist_t &owner, const pstring &name)
: object_t(DEVICE), logic_family_t()
#if (NL_KEEP_STATISTICS)
	, stat_total_time(0)
	, stat_update_count(0)
	, stat_call_count(0)
#endif
{
	if (logic_family() == nullptr)
		set_logic_family(family_TTL());
	init_object(owner, name);
}

ATTR_COLD core_device_t::core_device_t(core_device_t &owner, const pstring &name)
: object_t(DEVICE), logic_family_t()
#if (NL_KEEP_STATISTICS)
	, stat_total_time(0)
	, stat_update_count(0)
	, stat_call_count(0)
#endif
{
	set_logic_family(owner.logic_family());
	if (logic_family() == nullptr)
		set_logic_family(family_TTL());
	init_object(owner.netlist(), owner.name() + "." + name);
	owner.netlist().m_devices.push_back(plib::owned_ptr<core_device_t>(this, false));
}

ATTR_COLD core_device_t::~core_device_t()
{
}

ATTR_COLD void core_device_t::set_delegate_pointer()
{
#if (NL_KEEP_STATISTICS)
	netlist().m_started_devices.push_back(this);
#endif
#if (NL_PMF_TYPE == NL_PMF_TYPE_GNUC_PMF)
	void (core_device_t::* pFunc)() = &core_device_t::update;
	m_static_update = pFunc;
#elif (NL_PMF_TYPE == NL_PMF_TYPE_GNUC_PMF_CONV)
	void (core_device_t::* pFunc)() = &core_device_t::update;
	m_static_update = reinterpret_cast<net_update_delegate>((this->*pFunc));
#elif (NL_PMF_TYPE == NL_PMF_TYPE_INTERNAL)
	m_static_update = plib::mfp::get_mfp<net_update_delegate>(&core_device_t::update, this);
#endif
}

ATTR_COLD void core_device_t::stop_dev()
{
#if (NL_KEEP_STATISTICS)
#endif
	//stop();
}

ATTR_HOT netlist_sig_t core_device_t::INPLOGIC_PASSIVE(logic_input_t &inp)
{
	if (inp.state() != logic_t::STATE_INP_PASSIVE)
		return inp.Q();
	else
	{
		inp.activate();
		const netlist_sig_t ret = inp.Q();
		inp.inactivate();
		return ret;
	}
}


// ----------------------------------------------------------------------------------------
// device_t
// ----------------------------------------------------------------------------------------

device_t::~device_t()
{
	//log().debug("~net_device_t\n");
}

ATTR_COLD setup_t &device_t::setup()
{
	return netlist().setup();
}

ATTR_COLD void device_t::register_subalias(const pstring &name, core_terminal_t &term)
{
	pstring alias = this->name() + "." + name;

	// everything already fully qualified
	setup().register_alias_nofqn(alias, term.name());

	if (term.isType(terminal_t::INPUT) || term.isType(terminal_t::TERMINAL))
		m_terminals.push_back(alias);
}

ATTR_COLD void device_t::register_subalias(const pstring &name, const pstring &aliased)
{
	pstring alias = this->name() + "." + name;
	pstring aliased_fqn = this->name() + "." + aliased;

	// everything already fully qualified
	setup().register_alias_nofqn(alias, aliased_fqn);

	// FIXME: make this working again
	//if (term.isType(terminal_t::INPUT) || term.isType(terminal_t::TERMINAL))
	//  m_terminals.add(name);
}

ATTR_COLD void device_t::register_p(const pstring &name, object_t &obj)
{
	setup().register_object(*this, name, obj);
}

ATTR_COLD void device_t::connect_late(core_terminal_t &t1, core_terminal_t &t2)
{
	setup().register_link_fqn(t1.name(), t2.name());
}

ATTR_COLD void device_t::connect_late(const pstring &t1, const pstring &t2)
{
	setup().register_link_fqn(name() + "." + t1, name() + "." + t2);
}

/* FIXME: this is only used by solver code since matrix solvers are started in
 *        post_start.
 */
ATTR_COLD void device_t::connect_post_start(core_terminal_t &t1, core_terminal_t &t2)
{
	if (!setup().connect(t1, t2))
		netlist().log().fatal("Error connecting {1} to {2}\n", t1.name(), t2.name());
}


// -----------------------------------------------------------------------------
// family_setter_t
// -----------------------------------------------------------------------------

family_setter_t::family_setter_t(core_device_t &dev, const char *desc)
{
	dev.set_logic_family(dev.netlist().setup().family_from_model(desc));
}

family_setter_t::family_setter_t(core_device_t &dev, const logic_family_desc_t *desc)
{
	dev.set_logic_family(desc);
}

// ----------------------------------------------------------------------------------------
// net_t
// ----------------------------------------------------------------------------------------

ATTR_COLD net_t::net_t()
	: object_t(NET)
	, m_new_Q(0)
	, m_cur_Q (0)
	, m_railterminal(nullptr)
	, m_time(netlist_time::zero)
	, m_active(0)
	, m_in_queue(2)
	, m_cur_Analog(0.0)
{
}

ATTR_COLD net_t::~net_t()
{
	if (isInitialized())
		netlist().remove_save_items(this);
}

// FIXME: move somewhere central

struct do_nothing_deleter{
    template<typename T> void operator()(T*){}
};

ATTR_COLD void net_t::init_object(netlist_t &nl, const pstring &aname, core_terminal_t *mr)
{
	object_t::init_object(nl, aname);
	m_railterminal = mr;
	if (mr != nullptr)
		nl.m_nets.push_back(std::shared_ptr<net_t>(this, do_nothing_deleter()));
	else
		nl.m_nets.push_back(std::shared_ptr<net_t>(this));
}

ATTR_HOT void net_t::inc_active(core_terminal_t &term)
{
	m_active++;
	m_list_active.insert(term);
	nl_assert(m_active <= num_cons());
	if (m_active == 1)
	{
		if (netlist().use_deactivate())
		{
			railterminal().device().inc_active();
			//m_cur_Q = m_new_Q;
		}
		if (m_in_queue == 0)
		{
			if (m_time > netlist().time())
			{
				m_in_queue = 1;     /* pending */
				netlist().push_to_queue(*this, m_time);
			}
			else
			{
				m_cur_Q = m_new_Q;
				m_in_queue = 2;
			}
		}
		//else if (netlist().use_deactivate())
		//  m_cur_Q = m_new_Q;
	}
}

ATTR_HOT void net_t::dec_active(core_terminal_t &term)
{
	m_active--;
	nl_assert(m_active >= 0);
	m_list_active.remove(term);
	if (m_active == 0 && netlist().use_deactivate())
			railterminal().device().dec_active();
}

ATTR_COLD void net_t::rebuild_list()
{
	/* rebuild m_list */

	unsigned cnt = 0;
	m_list_active.clear();
	for (core_terminal_t *term : m_core_terms)
		if (term->state() != logic_t::STATE_INP_PASSIVE)
		{
			m_list_active.add(*term);
			cnt++;
		}
	m_active = cnt;
}

ATTR_COLD void net_t::save_register()
{
	save(NLNAME(m_time));
	save(NLNAME(m_active));
	save(NLNAME(m_in_queue));
	save(NLNAME(m_cur_Analog));
	save(NLNAME(m_cur_Q));
	save(NLNAME(m_new_Q));
	object_t::save_register();
}

ATTR_HOT /* inline */ void net_t::update_devs()
{
	//assert(m_num_cons != 0);
	nl_assert(this->isRailNet());

	const int masks[4] = { 1, 5, 3, 1 };
	const int mask = masks[ (m_cur_Q  << 1) | m_new_Q ];

	m_in_queue = 2; /* mark as taken ... */
	m_cur_Q = m_new_Q;

#if 1
	for (core_terminal_t *p = m_list_active.first(); p != nullptr; p = p->next())
	{
		inc_stat(p->device().stat_call_count);
		if ((p->state() & mask) != 0)
			p->device().update_dev();
	}
#else
	for (auto p = &m_list_active.m_head; *p != nullptr; )
	{
		auto pn = &((*p)->m_next);
		inc_stat(p->device().stat_call_count);
		if (((*p)->state() & mask) != 0)
			(*p)->device().update_dev();
		p = pn;
	}

#endif
}

ATTR_COLD void net_t::reset()
{
	m_time = netlist_time::zero;
	m_active = 0;
	m_in_queue = 2;

	m_new_Q = 0;
	m_cur_Q = 0;
	m_cur_Analog = 0.0;

	/* rebuild m_list */

	m_list_active.clear();
	for (core_terminal_t *ct : m_core_terms)
		m_list_active.add(*ct);

	for (core_terminal_t *ct : m_core_terms)
		ct->do_reset();

	for (core_terminal_t *ct : m_core_terms)
		if (ct->state() != logic_t::STATE_INP_PASSIVE)
			m_active++;
}

ATTR_COLD void net_t::register_con(core_terminal_t &terminal)
{
	terminal.set_net(this);

	m_core_terms.push_back(&terminal);

	if (terminal.state() != logic_t::STATE_INP_PASSIVE)
		m_active++;
}

ATTR_COLD void net_t::move_connections(net_t *dest_net)
{
	for (core_terminal_t *ct : m_core_terms)
		dest_net->register_con(*ct);
	m_core_terms.clear();
	m_active = 0;
}

ATTR_COLD void net_t::merge_net(net_t *othernet)
{
	netlist().log().debug("merging nets ...\n");
	if (othernet == nullptr)
		return; // Nothing to do

	if (othernet == this)
	{
		netlist().log().warning("Connecting {1} to itself. This may be right, though\n", this->name());
		return; // Nothing to do
	}

	if (this->isRailNet() && othernet->isRailNet())
		netlist().log().fatal("Trying to merge two rail nets: {1} and {2}\n", this->name(), othernet->name());

	if (othernet->isRailNet())
	{
		netlist().log().debug("othernet is railnet\n");
		othernet->merge_net(this);
	}
	else
	{
		othernet->move_connections(this);
	}
}


// ----------------------------------------------------------------------------------------
// logic_net_t
// ----------------------------------------------------------------------------------------

ATTR_COLD logic_net_t::logic_net_t()
	: net_t()
{
}


ATTR_COLD void logic_net_t::reset()
{
	net_t::reset();
}

ATTR_COLD void logic_net_t::save_register()
{
	net_t::save_register();
}

// ----------------------------------------------------------------------------------------
// analog_net_t
// ----------------------------------------------------------------------------------------

ATTR_COLD analog_net_t::analog_net_t()
	: net_t()
	, m_solver(nullptr)
{
}

ATTR_COLD analog_net_t::analog_net_t(netlist_t &nl, const pstring &aname)
	: net_t()
	, m_solver(nullptr)
{
	init_object(nl, aname);
}

ATTR_COLD void analog_net_t::reset()
{
	net_t::reset();
}

ATTR_COLD void analog_net_t::save_register()
{
	net_t::save_register();
}

ATTR_COLD bool analog_net_t::already_processed(plib::pvector_t<list_t> &groups)
{
	if (isRailNet())
		return true;
	for (auto & grp : groups)
	{
		if (grp.contains(this))
			return true;
	}
	return false;
}

ATTR_COLD void analog_net_t::process_net(plib::pvector_t<list_t> &groups)
{
	if (num_cons() == 0)
		return;
	/* add the net */
	groups.back().push_back(this);
	for (core_terminal_t *p : m_core_terms)
	{
		if (p->isType(terminal_t::TERMINAL))
		{
			terminal_t *pt = static_cast<terminal_t *>(p);
			analog_net_t *other_net = &pt->m_otherterm->net();
			if (!other_net->already_processed(groups))
				other_net->process_net(groups);
		}
	}
}


// ----------------------------------------------------------------------------------------
// core_terminal_t
// ----------------------------------------------------------------------------------------

ATTR_COLD core_terminal_t::core_terminal_t(const type_t atype)
: device_object_t(atype)
, plinkedlist_element_t()
, m_net(nullptr)
, m_state(STATE_NONEX)
{
}

ATTR_COLD core_terminal_t::core_terminal_t(core_device_t &dev, const type_t atype)
: device_object_t(dev, atype)
, plinkedlist_element_t()
, m_net(nullptr)
, m_state(STATE_NONEX)
{
}

ATTR_COLD core_terminal_t::core_terminal_t(core_device_t &dev, const pstring &aname, const type_t atype)
: device_object_t(dev, dev.name() + "." + aname, atype)
, plinkedlist_element_t()
, m_net(nullptr)
, m_state(STATE_NONEX)
{
}

ATTR_COLD void core_terminal_t::set_net(net_t::ptr_t anet)
{
	m_net = anet;
}

ATTR_COLD  void core_terminal_t::clear_net()
{
	m_net = nullptr;
}


// ----------------------------------------------------------------------------------------
// terminal_t
// ----------------------------------------------------------------------------------------

ATTR_COLD terminal_t::terminal_t(core_device_t &dev, const pstring &aname)
: analog_t(dev, aname, TERMINAL)
, m_otherterm(nullptr)
, m_Idr1(nullptr)
, m_go1(nullptr)
, m_gt1(nullptr)
{
	netlist().setup().register_object(dynamic_cast<device_t &>(dev), aname, *this);
}


ATTR_HOT void terminal_t::schedule_solve()
{
	// FIXME: Remove this after we found a way to remove *ALL* twoterms connected to railnets only.
	if (net().solver() != nullptr)
		net().solver()->update_forced();
}

ATTR_HOT void terminal_t::schedule_after(const netlist_time &after)
{
	// FIXME: Remove this after we found a way to remove *ALL* twoterms connected to railnets only.
	if (net().solver() != nullptr)
		net().solver()->update_after(after);
}

ATTR_COLD void terminal_t::reset()
{
	set_state(STATE_INP_ACTIVE);
	set_ptr(m_Idr1, 0.0);
	set_ptr(m_go1, netlist().gmin());
	set_ptr(m_gt1, netlist().gmin());
}

ATTR_COLD void terminal_t::save_register()
{
	save(NLNAME(m_Idr1));
	save(NLNAME(m_go1));
	save(NLNAME(m_gt1));
	core_terminal_t::save_register();
}


// ----------------------------------------------------------------------------------------
// net_input_t
// ----------------------------------------------------------------------------------------

// ----------------------------------------------------------------------------------------
// net_output_t
// ----------------------------------------------------------------------------------------

// ----------------------------------------------------------------------------------------
// logic_output_t
// ----------------------------------------------------------------------------------------

ATTR_COLD logic_output_t::logic_output_t()
	: logic_t(OUTPUT)
{
	set_state(STATE_OUT);
	this->set_net(&m_my_net);
}

ATTR_COLD void logic_output_t::init_object(core_device_t &dev, const pstring &aname)
{
	core_terminal_t::init_object(dev, aname);
	net().init_object(dev.netlist(), aname + ".net", this);
}

ATTR_COLD void logic_output_t::initial(const netlist_sig_t val)
{
	net().as_logic().initial(val);
}


// ----------------------------------------------------------------------------------------
// analog_output_t
// ----------------------------------------------------------------------------------------

ATTR_COLD analog_output_t::analog_output_t(core_device_t &dev, const pstring &aname)
	: analog_t(dev, aname, OUTPUT), m_proxied_net(nullptr)
{
	this->set_net(&m_my_net);
	set_state(STATE_OUT);

	net().m_cur_Analog = NL_FCONST(0.0);
	net().init_object(dev.netlist(), name() + ".net", this);
	netlist().setup().register_object(dynamic_cast<device_t &>(dev), aname, *this);
}

#if 0
ATTR_COLD analog_output_t::analog_output_t()
	: analog_t(OUTPUT), m_proxied_net(nullptr)
{
	this->set_net(&m_my_net);
	set_state(STATE_OUT);

	//net().m_cur_Analog = NL_FCONST(0.99);
	net().m_cur_Analog = NL_FCONST(0.0);
}
#endif

ATTR_COLD void analog_output_t::init_object(core_device_t &dev, const pstring &aname)
{
	analog_t::init_object(dev, aname);
	net().init_object(dev.netlist(), aname + ".net", this);
}

ATTR_COLD void analog_output_t::initial(const nl_double val)
{
	net().m_cur_Analog = val;
}

// ----------------------------------------------------------------------------------------
// param_t & friends
// ----------------------------------------------------------------------------------------

ATTR_COLD param_t::param_t(const param_type_t atype, device_t &device, const pstring &name)
	: device_object_t(device, name, PARAM)
	, m_param_type(atype)
{
}

ATTR_COLD const pstring param_model_t::model_type()
{
	if (m_map.size() == 0)
		netlist().setup().model_parse(this->Value(), m_map);
	return m_map["COREMODEL"];
}


ATTR_COLD const pstring param_model_t::model_value_str(const pstring &entity)
{
	if (m_map.size() == 0)
		netlist().setup().model_parse(this->Value(), m_map);
	return netlist().setup().model_value_str(m_map, entity);
}

ATTR_COLD nl_double param_model_t::model_value(const pstring &entity)
{
	if (m_map.size() == 0)
		netlist().setup().model_parse(this->Value(), m_map);
	return netlist().setup().model_value(m_map, entity);
}

} // namespace

namespace netlist
{
	namespace devices
	{

	// ----------------------------------------------------------------------------------------
	// mainclock
	// ----------------------------------------------------------------------------------------

	ATTR_HOT /* inline */ void NETLIB_NAME(mainclock)::mc_update(logic_net_t &net)
	{
		net.toggle_new_Q();
		net.update_devs();
	}


	} //namespace devices
} // namespace netlist

