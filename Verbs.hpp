#pragma once

#include "MPIConnection.hpp"

#include <infiniband/arch.h>
#include <infiniband/verbs.h>

#include <vector>

///
/// This class does the bare minimum to up IBVerbs queue pairs between
/// all processes in an MPI job for RDMA communications. It uses MPI
/// to exchange information during queue pair setup.
///
/// It assumes you won't be using Send/Receive Verbs and doesn't post
/// any receive buffers, although you certainly can if you want.
///
/// It assumes you will be running multiple processes per node, and
/// gives each process two IDs ("ranks") and synchronization domains:
///
///  - one that is valid across all processes on all nodes in the job,
///    where processes on the same node have contiguous ranks, and
///
///  - another that is local to each node/locale, to support
///  - node-local barriers that other nodes do not participate in.
///
/// NOTE: there are a number of parameters that can be tuned for
/// specific use cases below.
///
/// TODO/NOTE: it is possible to issue send requests too fast for the
/// card. If that happens, this code will print an error and exit. I
/// have come other code that detects this condition and blocks/limits
/// the request rate instead; I'll integrate it if its needed.
///
class Verbs {
  /// list of Verbs-capable devices
  ibv_device ** devices;
  int num_devices;

  /// info about chosen device
  ibv_device * device;
  const char * device_name;
  uint64_t device_guid;
  ibv_device_attr device_attributes;

  /// info about chosen port
  uint8_t port;
  ibv_port_attr port_attributes;

  /// device context, used for most Verbs operations
  ibv_context * context;

  /// protection domain to go with context
  ibv_pd * protection_domain;

  /// constants for initializing queues
  static const int completion_queue_depth = 256;
  static const int send_queue_depth    = 16;         // how many operations per queue should we be able to enqueue at a time?
  static const int receive_queue_depth = 1;          // only need 1 if we're just using RDMA ops
  static const int scatter_gather_element_count = 1; // how many SGE's do we allow per operation?
  static const int max_inline_data = 16;             // message rate drops from 6M/s to 4M/s at 29 bytes
  static const int max_dest_rd_atomic = 16;          // how many outstanding reads/atomic ops are allowed? (remote end of qp, limited by card)
  static const int max_rd_atomic = 16;               // how many outstanding reads/atomic ops are allowed? (local end of qp, limited by card)
  static const int min_rnr_timer = 0x12;             // from Mellanox RDMA-Aware Programming manual; probably don't need to touch
  static const int timeout = 0x12;                   // from Mellanox RDMA-Aware Programming manual; probably don't need to touch
  static const int retry_count = 6;                  // from Mellanox RDMA-Aware Programming manual; probably don't need to touch
  static const int rnr_retry = 0;                    // from Mellanox RDMA-Aware Programming manual; probably don't need to touch

  /// completion queue, shared across all endpoints/queue pairs
  ibv_cq * completion_queue;

  /// info about each endpoint (rank/process) in job
  struct Endpoint {
    uint16_t lid;        // InfiniBand address of node
    uint32_t qp_num;     // Queue pair number on node (like IP port number)
    ibv_qp * queue_pair;
  };

  /// array of endpoints, one per rank
  std::vector< Endpoint > endpoints;


  
  /// Discover local Verbs-capable devices; choose one and prepare it for use.
  void initialize_device( const std::string desired_device_name, const int8_t desired_port );

  /// set up queue pairs for RDMA operations
  void connect_queue_pairs();

  /// release resources on device in preparation for shutting down
  void finalize_device();
  
public:
  /// MPIConnection reference for communication during queue pair and memory region setup
  MPIConnection & m;
  
  Verbs( MPIConnection & m, const std::string desired_device_name = "mlx4_0", const int8_t desired_port = 1 )
    : devices( nullptr )
    , num_devices( 0 )
    , device( nullptr )
    , device_name( nullptr )
    , device_guid( 0 )
    , device_attributes()
    , port( 0 )
    , port_attributes()
    , context( nullptr )
    , protection_domain( nullptr )
    , completion_queue( nullptr )
    , endpoints()
    , m( m )
  {
    initialize_device( desired_device_name, desired_port );
    connect_queue_pairs();
  }

  /// call before ending process
  void finalize() {
    finalize_device();
  }

  /// destructor ensures finalize has been called
  ~Verbs() {
    finalize();
  }

  /// Accessor for protection domain
  ibv_pd * get_protection_domain() const { return protection_domain; }

  /// Register a region of memory with Verbs library
  ibv_mr * register_memory_region( void * base, size_t size );

  /// post a receive request for a remote rank
  void post_receive( int remote_rank, ibv_recv_wr * wr );

  /// post a send request to a remote rank
  void post_send( int remote_rank, ibv_send_wr * wr );

  /// consume up to max_entries completion queue entries. Returns number of entries consumed.
  int poll( int max_entries = 1 );

};
