
#SHA-DB: Simple Highly Available DataBase 

Assume a database service that can contain up to N entries of the form (k,v), where k and v are integers. The database is initially empty, that is, it contains no entries. Clients can submit two operations to the database service: read and write. The "read" operation takes one input parameter, k, and returns value v if entry (k,v) exists in the storage or null if the entry does not exist. The "write" operation takes input parameters k and v, and creates entry (k,v) in the database if there is no entry of the form (k,s) in the database; if entry (k,s) exists, the operation replaces the value of s with v. 

My task is to implement a highly available service. There is one client, who submits read and write operations to the replicas. The database is replicated in two servers. Use Paxos (libpaxos3) to ensure consistency of the replicas. 


#LibPaxos

This is LibPaxos3! A complete rewrite of [LibPaxos2][1].
LibPaxos3 has been improved in the following aspects:

- Doesn't make use of multicast
- Has a cleaner design
- Has a better build system based on CMake
- Comes with unit tests

LibPaxos3 is divided in two libraries: libpaxos and libevpaxos. 

Libpaxos (see ```libpaxos/paxos```) implements the core of the Paxos consensus 
protocol, and is not cluttered with network specific code. That is, libpaxos 
does not depend on any particular networking library.

Libevpaxos (see ```libpaxos/evpaxos```) is the actual networked Paxos 
implementation. This library is built on top of the libpaxos and [libevent][2].

## Building

These are the basic steps required to get and compile LibPaxos3

	git clone https://bitbucket.org/sciascid/libpaxos.git
	mkdir libpaxos/build
	cd libpaxos/build
	cmake ..
	make

LibPaxos3 depends on [libevent][2] and [msgpack][9]. By default, LibPaxos3 uses
an in-memory storage, with support for a storage backend based on [LMDB][10]
(see ```paxos.conf``` and build options below).

LibPaxos3 should compile on Linux and OS X.

### Useful build options

You pass options to cmake as follows: ```cmake -DOPTION=VALUE```

- ```LMDB_ROOT=PATH```  - point it to your installation of LMDB
- ```LIBEVENT_ROOT=PATH``` -  point it to your installation of Libevent
- ```MSGPACK_ROOT=PATH``` - point it to your installation of MessagePack

## Running the examples

	cd libpaxos/build
	./sample/acceptor 0 ../paxos.conf > /dev/null &
	./sample/acceptor 1 ../paxos.conf > /dev/null &
	./sample/proposer 0 ../paxos.conf > /dev/null &
	./sample/learner ../paxos.conf > learner.txt &
	./sample/client 127.0.0.1:5550 1

## Configuration

See ```paxos.conf``` for a sample configuration file.

##  Unit tests

Unit tests depend on the [Google Test][4] library. Execute the tests using 
```make test``` in your build directory, or run ```runtest``` from
```build/unit``` for  detailed output.

## Feedback

[LibPaxos project page][1]

[LibPaxos3 repository][5]

[Mailing list][6]

## License

LibPaxos3 is distributed under the terms of the 3-clause BSD license.
LibPaxos3 has been developed at the [University of Lugano][7],
by [Daniele Sciascia][8].

[1]: http://libpaxos.sourceforge.net
[2]: http://www.libevent.org
[4]: http://code.google.com/p/googletest/
[5]: https://bitbucket.org/sciascid/libpaxos
[6]: https://lists.sourceforge.net/lists/listinfo/libpaxos-general
[7]: http://inf.usi.ch
[8]: http://atelier.inf.usi.ch/~sciascid
[9]: http://www.msgpack.org
[10]: http://symas.com/mdb/
