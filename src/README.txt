Vios-proxy is network tunneling suite that uses virtioserial channels
to connect a server runing in a QEMU host to clients running in
QEMU guests.

Vios-proxy consists of two daemon programs: 

 * vios-proxy-host runs in the QEMU host. It listens for connections
   from guest clients over virtioserial ports. Upon receiving a 
   connection, vios-proxy-host connects to the localhost network port
   of the proxied service and begins sending and receiving data
   through the tunnel.

 * vios-proxy-guest runs in each QEMU guest. It creates a listening
   network socket on localhost for the service being proxied. Clients
   in the QEMU guest connect to this network port. Upon connection
   with a client, the vios-proxy-guest negotiates a connection with 
   the vios-proxy-host over the virtioserial channel. 

Vios-proxy is available via git:

URL	git://git.fedorahosted.org/vios-proxy.git
	ssh://git.fedorahosted.org/git/vios-proxy.git
	http://git.fedorahosted.org/git/vios-proxy.git

You can read more about vios-proxy here:

  doc/ViosOverview.odt

  

