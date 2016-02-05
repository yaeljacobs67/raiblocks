#pragma once

#include <rai/node/bootstrap.hpp>
#include <rai/node/wallet.hpp>

#include <unordered_set>
#include <memory>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>

#include <boost/asio.hpp>
#include <boost/iostreams/device/array.hpp>
#include <boost/log/trivial.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/circular_buffer.hpp>

std::ostream & operator << (std::ostream &, std::chrono::system_clock::time_point const &);

namespace boost
{
namespace program_options
{
class options_description;
class variables_map;
}
}

namespace rai
{
class node;
class election : public std::enable_shared_from_this <rai::election>
{
public:
    election (std::shared_ptr <rai::node>, rai::block const &, std::function <void (rai::block &)> const &);
    void vote (rai::vote const &);
	void interval_action ();
    void start_request (rai::block const &);
	void confirm (bool);
    rai::uint128_t uncontested_threshold (MDB_txn *, rai::ledger &);
    rai::uint128_t contested_threshold (MDB_txn *, rai::ledger &);
    rai::votes votes;
    std::weak_ptr <rai::node> node;
    std::chrono::system_clock::time_point last_vote;
	std::unique_ptr <rai::block> last_winner;
    bool confirmed;
	std::function <void (rai::block &)> confirmation_action;
};
class conflict_info
{
public:
	rai::block_hash root;
	std::shared_ptr <rai::election> election;
	// Number of announcements in a row for this fork
	int announcements;
};
class conflicts
{
public:
    conflicts (rai::node &);
    void start (rai::block const &, std::function <void (rai::block &)> const &, bool);
    bool no_conflict (rai::block_hash const &);
    void update (rai::vote const &);
	void announce_votes ();
    boost::multi_index_container
	<
		rai::conflict_info,
		boost::multi_index::indexed_by
		<
			boost::multi_index::ordered_unique <boost::multi_index::member <rai::conflict_info, rai::block_hash, &rai::conflict_info::root>>
		>
	> roots;
    rai::node & node;
    std::mutex mutex;
	static size_t constexpr announcements_per_interval = 32;
	static size_t constexpr contigious_announcements = 4;
};
class operation
{
public:
    bool operator > (rai::operation const &) const;
    std::chrono::system_clock::time_point wakeup;
    std::function <void ()> function;
};
class processor_service
{
public:
    processor_service ();
    void run ();
    size_t poll ();
    size_t poll_one ();
    void add (std::chrono::system_clock::time_point const &, std::function <void ()> const &);
    void stop ();
    bool stopped ();
    size_t size ();
    bool done;
    std::mutex mutex;
    std::condition_variable condition;
    std::priority_queue <operation, std::vector <operation>, std::greater <operation>> operations;
};
class gap_information
{
public:
    std::chrono::system_clock::time_point arrival;
    rai::block_hash required;
    rai::block_hash hash;
	std::unique_ptr <rai::votes> votes;
    std::unique_ptr <rai::block> block;
};
class gap_cache
{
public:
    gap_cache (rai::node &);
    void add (rai::block const &, rai::block_hash);
    std::vector <std::unique_ptr <rai::block>> get (rai::block_hash const &);
    void vote (MDB_txn *, rai::vote const &);
    rai::uint128_t bootstrap_threshold (MDB_txn *);
    boost::multi_index_container
    <
        rai::gap_information,
        boost::multi_index::indexed_by
        <
            boost::multi_index::hashed_non_unique <boost::multi_index::member <gap_information, rai::block_hash, &gap_information::required>>,
            boost::multi_index::ordered_non_unique <boost::multi_index::member <gap_information, std::chrono::system_clock::time_point, &gap_information::arrival>>,
            boost::multi_index::hashed_unique <boost::multi_index::member <gap_information, rai::block_hash, &gap_information::hash>>
        >
    > blocks;
    size_t const max = 16384;
    std::mutex mutex;
    rai::node & node;
};
class work_pool;
class peer_information
{
public:
	rai::endpoint endpoint;
	std::chrono::system_clock::time_point last_contact;
	std::chrono::system_clock::time_point last_attempt;
	std::chrono::system_clock::time_point last_bootstrap_failure;
	rai::block_hash most_recent;
};
class peer_container
{
public:
	peer_container (rai::endpoint const &);
	// We were contacted by endpoint, update peers
    void contacted (rai::endpoint const &);
	// Unassigned, reserved, self
	bool not_a_peer (rai::endpoint const &);
	// Returns true if peer was already known
	bool known_peer (rai::endpoint const &);
	// Notify of peer we received from
	bool insert (rai::endpoint const &);
	// Received from a peer and contained a block announcement
	bool insert (rai::endpoint const &, rai::block_hash const &);
	// Does this peer probably know about this block
	bool knows_about (rai::endpoint const &, rai::block_hash const &);
	// Notify of bootstrap failure
	void bootstrap_failed (rai::endpoint const &);
	void random_fill (std::array <rai::endpoint, 8> &);
	// List of all peers
	std::vector <peer_information> list ();
	// List of peers that haven't failed bootstrapping in a while
	std::vector <peer_information> bootstrap_candidates ();
	// Purge any peer where last_contact < time_point and return what was left
	std::vector <rai::peer_information> purge_list (std::chrono::system_clock::time_point const &);
	size_t size ();
	bool empty ();
	std::mutex mutex;
	rai::endpoint self;
	boost::multi_index_container
	<
		peer_information,
		boost::multi_index::indexed_by
		<
			boost::multi_index::hashed_unique <boost::multi_index::member <peer_information, rai::endpoint, &peer_information::endpoint>>,
			boost::multi_index::ordered_non_unique <boost::multi_index::member <peer_information, std::chrono::system_clock::time_point, &peer_information::last_contact>>,
			boost::multi_index::ordered_non_unique <boost::multi_index::member <peer_information, std::chrono::system_clock::time_point, &peer_information::last_attempt>, std::greater <std::chrono::system_clock::time_point>>
		>
	> peers;
	std::function <void (rai::endpoint const &)> peer_observer;
	std::function <void ()> disconnect_observer;
};
class send_info
{
public:
	uint8_t const * data;
	size_t size;
	rai::endpoint endpoint;
	size_t rebroadcast;
	std::function <void (boost::system::error_code const &, size_t)> callback;
};
class network
{
public:
    network (boost::asio::io_service &, uint16_t, rai::node &);
    void receive ();
    void stop ();
    void receive_action (boost::system::error_code const &, size_t);
    void rpc_action (boost::system::error_code const &, size_t);
    void republish_block (std::unique_ptr <rai::block>, size_t);
    void publish_broadcast (std::vector <rai::peer_information> &, std::unique_ptr <rai::block>);
    bool confirm_broadcast (std::vector <rai::peer_information> &, std::unique_ptr <rai::block>, uint64_t, size_t);
	void confirm_block (rai::raw_key const &, rai::public_key const &, std::unique_ptr <rai::block>, uint64_t, rai::endpoint const &, size_t);
    void merge_peers (std::array <rai::endpoint, 8> const &);
    void send_keepalive (rai::endpoint const &);
	void broadcast_confirm_req (rai::block const &);
    void send_confirm_req (rai::endpoint const &, rai::block const &);
	void initiate_send ();
    void send_buffer (uint8_t const *, size_t, rai::endpoint const &, size_t, std::function <void (boost::system::error_code const &, size_t)>);
    void send_complete (boost::system::error_code const &, size_t);
    rai::endpoint endpoint ();
    rai::endpoint remote;
    std::array <uint8_t, 512> buffer;
    boost::asio::ip::udp::socket socket;
    std::mutex socket_mutex;
    boost::asio::io_service & service;
    boost::asio::ip::udp::resolver resolver;
    rai::node & node;
    uint64_t bad_sender_count;
    std::queue <rai::send_info> sends;
    bool on;
    uint64_t keepalive_count;
    uint64_t publish_count;
    uint64_t confirm_req_count;
    uint64_t confirm_ack_count;
    uint64_t insufficient_work_count;
    uint64_t error_count;
    static uint16_t const node_port = rai::rai_network == rai::rai_networks::rai_live_network ? 7075 : 54000;
};
class logging
{
public:
	logging ();
    void serialize_json (boost::property_tree::ptree &) const;
	bool deserialize_json (boost::property_tree::ptree const &);
    bool ledger_logging () const;
    bool ledger_duplicate_logging () const;
    bool network_logging () const;
    bool network_message_logging () const;
    bool network_publish_logging () const;
    bool network_packet_logging () const;
    bool network_keepalive_logging () const;
    bool node_lifetime_tracing () const;
    bool insufficient_work_logging () const;
    bool log_rpc () const;
    bool bulk_pull_logging () const;
    bool work_generation_time () const;
    bool log_to_cerr () const;
	bool ledger_logging_value;
	bool ledger_duplicate_logging_value;
	bool network_logging_value;
	bool network_message_logging_value;
	bool network_publish_logging_value;
	bool network_packet_logging_value;
	bool network_keepalive_logging_value;
	bool node_lifetime_tracing_value;
	bool insufficient_work_logging_value;
	bool log_rpc_value;
	bool bulk_pull_logging_value;
	bool work_generation_time_value;
	bool log_to_cerr_value;
	uintmax_t max_size;
};
class node_init
{
public:
    node_init ();
    bool error ();
    bool block_store_init;
    bool wallet_init;
};
class node_config
{
public:
	node_config ();
	node_config (uint16_t, rai::logging const &);
    void serialize_json (boost::property_tree::ptree &) const;
	bool deserialize_json (bool &, boost::property_tree::ptree &);
	rai::account random_representative ();
	uint16_t peering_port;
	rai::logging logging;
	std::vector <std::pair <boost::asio::ip::address, uint16_t>> work_peers;
	std::vector <std::string> preconfigured_peers;
	std::vector <rai::account> preconfigured_representatives;
	unsigned packet_delay_microseconds;
	unsigned bootstrap_fraction_numerator;
	unsigned creation_rebroadcast;
	unsigned rebroadcast_delay;
	rai::amount receive_minimum;
    static std::chrono::seconds constexpr keepalive_period = std::chrono::seconds (60);
    static std::chrono::seconds constexpr keepalive_cutoff = keepalive_period * 5;
	static std::chrono::minutes constexpr wallet_backup_interval = std::chrono::minutes (5);
};
class node : public std::enable_shared_from_this <rai::node>
{
public:
    node (rai::node_init &, boost::asio::io_service &, uint16_t, boost::filesystem::path const &, rai::processor_service &, rai::logging const &, rai::work_pool &);
    node (rai::node_init &, boost::asio::io_service &, boost::filesystem::path const &, rai::processor_service &, rai::node_config const &, rai::work_pool &);
    ~node ();
	template <typename T>
	void background (T action_a)
	{
		service.add (std::chrono::system_clock::now (), action_a);
	}
    void send_keepalive (rai::endpoint const &);
	void keepalive (std::string const &, uint16_t);
    void start ();
    void stop ();
    std::shared_ptr <rai::node> shared ();
    bool representative_vote (rai::election &, rai::block const &);
	int store_version ();
    void vote (rai::vote const &);
    void process_confirmed (rai::block const &);
	void process_message (rai::message &, rai::endpoint const &);
    void process_confirmation (rai::block const &, rai::endpoint const &);
    void process_receive_republish (std::unique_ptr <rai::block>, size_t);
    void process_receive_many (rai::transaction &, rai::block const &, std::function <void (rai::process_return, rai::block const &)> = [] (rai::process_return, rai::block const &) {});
    rai::process_return process_receive_one (rai::transaction &, rai::block const &);
	rai::process_return process (rai::block const &);
    void keepalive_preconfigured (std::vector <std::string> const &);
	rai::block_hash latest (rai::account const &);
	rai::uint128_t balance (rai::account const &);
	rai::uint128_t weight (rai::account const &);
	rai::account representative (rai::account const &);
	void call_observers (rai::block const & block_a, rai::account const & account_a, rai::amount const &);
    void ongoing_keepalive ();
	void backup_wallet ();
	int price (rai::uint128_t const &, int);
	void generate_work (rai::block &);
	uint64_t generate_work (rai::uint256_union const &);
	rai::node_config config;
    rai::processor_service & service;
	rai::work_pool & work;
    boost::log::sources::logger log;
    rai::block_store store;
    rai::gap_cache gap_cache;
    rai::ledger ledger;
    rai::conflicts conflicts;
    rai::wallets wallets;
    rai::network network;
	rai::bootstrap_initiator bootstrap_initiator;
    rai::bootstrap_listener bootstrap;
    rai::peer_container peers;
	boost::filesystem::path application_path;
    std::vector <std::function <void (rai::block const &, rai::account const &, rai::amount const &)>> observers;
	std::vector <std::function <void (rai::account const &, bool)>> wallet_observers;
    std::vector <std::function <void (rai::vote const &)>> vote_observers;
	std::vector <std::function <void (rai::endpoint const &)>> endpoint_observers;
	std::vector <std::function <void ()>> disconnect_observers;
	static double constexpr price_max = 16.0;
	static double constexpr free_cutoff = 1024.0;
    static std::chrono::seconds constexpr period = std::chrono::seconds (60);
    static std::chrono::seconds constexpr cutoff = period * 5;
	static std::chrono::minutes constexpr backup_interval = std::chrono::minutes (5);
};
class thread_runner
{
public:
	thread_runner (boost::asio::io_service &, rai::processor_service &);
	void join ();
	std::vector <std::thread> threads;
};
void add_node_options (boost::program_options::options_description &);
bool handle_node_options (boost::program_options::variables_map &);
class inactive_node
{
public:
	inactive_node ();
	boost::shared_ptr <boost::asio::io_service> service;
	rai::processor_service processor;
	rai::logging logging;
	rai::node_init init;
	rai::work_pool work;
	std::shared_ptr <rai::node> node;
};
extern std::chrono::milliseconds const confirm_wait;
}