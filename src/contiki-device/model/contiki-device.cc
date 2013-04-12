#include <semaphore.h>
#include <sys/mman.h>
#include <fcntl.h>

#include "contiki-device.h"


NS_LOG_COMPONENT_DEFINE ("ContikiNetDevice");

namespace ns3 {

IpcReader::Data ContikiIpcReader::DoRead (void)
{
	NS_LOG_FUNCTION_NOARGS ();

	uint32_t bufferSize = 65536;
	uint8_t *buf = (uint8_t *)malloc (bufferSize);
	//char *buf = (char *)malloc(bufferSize);
	NS_ABORT_MSG_IF (buf == 0, "malloc() failed");

	if(sem_wait(m_sem_in) == -1)
		NS_FATAL_ERROR("sem_wait() failed: " << strerror(errno));

	void *retval = memcpy((void *)buf,m_traffic_in,bufferSize);

	if(sem_post(m_sem_in) == -1)
		NS_FATAL_ERROR("sem_post() failed: " << strerror(errno));

	if (retval == (void *) -1)
	{
		NS_LOG_INFO ("ContikiNetDeviceFdReader::DoRead(): done");
		free (buf);
		buf = 0;
		bufferSize = -1;
		//len = 0;
	}


	return IpcReader::Data (buf, bufferSize);
}

NS_OBJECT_ENSURE_REGISTERED (ContikiNetDevice);

TypeId
ContikiNetDevice::GetTypeId (void)
{
	static TypeId tid = TypeId ("ns3::ContikiNetDevice")
    								.SetParent<NetDevice> ()
    								.AddConstructor<ContikiNetDevice> ()
    								.AddAttribute ("Start",
    										"The simulation time at which to spin up the socket device read thread.",
    										TimeValue (Seconds (0.)),
    										MakeTimeAccessor (&ContikiNetDevice::m_tStart),
    										MakeTimeChecker ())
    										.AddAttribute ("Stop",
    												"The simulation time at which to tear down the socket device read thread.",
    												TimeValue (Seconds (0.)),
    												MakeTimeAccessor (&ContikiNetDevice::m_tStop),
    												MakeTimeChecker ())
    												;
	return tid;
}

static void * m_traffic_time = NULL;
ContikiNetDevice::ContikiNetDevice ()
: m_node (0),
  m_ifIndex (0),
  m_traffic_in (NULL),
  m_traffic_out (NULL),
  m_startEvent (),
  m_stopEvent (),
  m_ipcReader (0)
{
	NS_LOG_FUNCTION_NOARGS ();
	m_packetBuffer = new uint8_t[65536];
	Start (m_tStart);
}

ContikiNetDevice::~ContikiNetDevice()
{
	NS_LOG_FUNCTION_NOARGS ();

	StopContikiDevice ();

	delete [] m_packetBuffer;
	m_packetBuffer = 0;
	m_bridgedDevice = 0;
}

void
ContikiNetDevice::DoDispose ()
{
	NS_LOG_FUNCTION_NOARGS ();
	NetDevice::DoDispose ();
}

void
ContikiNetDevice::Start (Time tStart)
{
	NS_LOG_FUNCTION (tStart);

	//
	// Cancel any pending start event and schedule a new one at some relative time in the future.
	//
	Simulator::Cancel (m_startEvent);
	m_startEvent = Simulator::Schedule (tStart, &ContikiNetDevice::StartContikiDevice, this);
}

void
ContikiNetDevice::Stop (Time tStop)
{
	NS_LOG_FUNCTION (tStop);
	//
	// Cancel any pending stop event and schedule a new one at some relative time in the future.
	//
	Simulator::Cancel (m_stopEvent);
	m_stopEvent = Simulator::Schedule (tStop, &ContikiNetDevice::StopContikiDevice, this);
}

void
ContikiNetDevice::StartContikiDevice (void)
{
	NS_LOG_FUNCTION_NOARGS ();

	NS_ABORT_MSG_IF (m_traffic_in != NULL, "ContikiNetDevice::StartContikiDevice(): IPC Shared Memory already created");

	m_nodeId = GetNode ()->GetId ();

	//
	// Spin up the Contiki Device and start receiving packets.
	//
	NS_LOG_LOGIC ("Creating IPC Shared Memory");


	Simulator::GetImplementation()->TraceConnectWithoutContext("CurrentTs", MakeCallback(&ContikiNetDevice::ContikiClockHandle, this));
	CreateIpc ();

	//
	// Now spin up a read thread to read packets from the tap device.
	//
	NS_ABORT_MSG_IF (m_ipcReader != 0,"ContikiNetDevice::StartContikiDevice(): Receive thread is already running");
	NS_LOG_LOGIC ("Spinning up read thread");

	m_ipcReader = Create<ContikiIpcReader> ();
	m_ipcReader->Start (m_traffic_in, MakeCallback (&ContikiNetDevice::ReadCallback, this), m_nodeId);
}

void
ContikiNetDevice::ContikiClockHandle(uint64_t oldValue, uint64_t newValue)
{
	if( sem_wait(m_sem_time) == -1)
		NS_FATAL_ERROR("sem_wait() failed: " << strerror(errno));

	memcpy(m_traffic_time, (void *)&newValue, m_time_size);

}

void
ContikiNetDevice::StopContikiDevice (void)
{
	NS_LOG_FUNCTION_NOARGS ();

	if (m_ipcReader != 0)
	{
		m_ipcReader->Stop ();
		m_ipcReader = 0;
	}

	//unmapping mapped memory addresses and closing semaphore variables

	if (m_traffic_in != NULL)
	{
		if(munmap(m_traffic_in, m_traffic_size) == -1)
			NS_FATAL_ERROR("munmap() failed: " << strerror(errno));

		m_traffic_in = NULL;
	}

	if (m_traffic_out != NULL)
	{
		if(munmap(m_traffic_out, m_traffic_size) == -1)
			NS_FATAL_ERROR("munmap() failed: " << strerror(errno));

		m_traffic_out = NULL;
	}

	if (m_traffic_time != NULL)
		{
			if(munmap(m_traffic_time, m_time_size) == -1)
				NS_FATAL_ERROR("munmap() failed: " << strerror(errno));

			m_traffic_time = NULL;
		}

	NS_LOG_UNCOND("Killing Child");
	kill(child,SIGKILL);

	shm_unlink(m_shm_in_name.str().c_str());
	shm_unlink(m_shm_out_name.str().c_str());
	shm_unlink(m_shm_time_name.str().c_str());

	sem_unlink(m_sem_in_name.str().c_str());
	sem_unlink(m_sem_out_name.str().c_str());
	sem_unlink(m_sem_time_name.str().c_str());

}

void
ContikiNetDevice::CreateIpc (void)
{
	NS_LOG_FUNCTION_NOARGS ();

	Ptr<NetDevice> nd = GetBridgedNetDevice ();
	Ptr<Node> n = nd->GetNode ();

	/* Generate MAC address, assign to Node */
	Mac64Address mac64Address = Mac64Address::Allocate();

	Address ndAddress = Address(mac64Address);
	nd->SetAddress(ndAddress);

	/* Shared Memory*/


	m_shm_in_name << "./traffic_in_" << m_nodeId;
	if((m_shm_in = shm_open(m_shm_in_name.str().c_str(),O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) == -1)
		NS_FATAL_ERROR("shm_open()" << strerror(errno));

	m_shm_out_name << "./traffic_out_" << m_nodeId;
	if((m_shm_out = shm_open(m_shm_out_name.str().c_str(),O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) == -1)
		NS_FATAL_ERROR("shm_open()" << strerror(errno));

	m_shm_time_name << "./traffic_time_" << m_nodeId;
	if ((m_shm_time = shm_open(m_shm_time_name.str().c_str(),O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) == -1)
		NS_FATAL_ERROR("shm_open()" << strerror(errno));

	if(ftruncate(m_shm_in, m_traffic_size) == -1 \
			|| ftruncate(m_shm_out, m_traffic_size) == -1 \
			|| ftruncate(m_shm_time, m_time_size) == -1)
		NS_FATAL_ERROR("shm_open()" << strerror(errno));

	m_traffic_in = mmap(NULL, m_traffic_size, PROT_READ | PROT_WRITE, MAP_SHARED, m_shm_in, 0);
	m_traffic_out = mmap(NULL, m_traffic_size, PROT_READ | PROT_WRITE, MAP_SHARED, m_shm_out, 0);

	if(m_traffic_time == NULL)
		m_traffic_time = mmap(NULL, m_time_size, PROT_READ | PROT_WRITE, MAP_SHARED, m_shm_time, 0);

	m_sem_in_name << "./sem_in_" << m_nodeId;
	if((m_sem_in = sem_open(m_sem_in_name.str().c_str(), O_RDWR | O_CREAT | O_EXCL,
			S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH, 1)) == SEM_FAILED)
		NS_FATAL_ERROR("sem_open() failed: " << strerror(errno));

	m_sem_out_name << "./sem_out_" << m_nodeId;
	if ((m_sem_out = sem_open(m_sem_out_name.str().c_str(), O_RDWR | O_CREAT | O_EXCL,
			S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH, 1)) == SEM_FAILED)
		NS_FATAL_ERROR("sem_open() failed: " << strerror(errno));

	m_sem_time_name << "./sem_time_ns3";
	if ((m_sem_time = sem_open(m_sem_time_name.str().c_str(), O_RDWR | O_CREAT,
			S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH, 1)) == SEM_FAILED)
		NS_FATAL_ERROR("sem_open() failed: " << strerror(errno));

	/* Forking Contiki */

	if ((child = fork()) == -1)
		NS_ABORT_MSG ("ContikINetDevice::CreateIpc(): Unix fork error, errno = " << strerror (errno));
	else if (child) {   /*  This is the parent. */
		NS_LOG_DEBUG ("Parent process");
		//NS_LOG_UNCOND("Child PID: " << child);
	} else {

	}


}

void
ContikiNetDevice::ReadCallback (uint8_t *buf, ssize_t len)
{
	NS_LOG_FUNCTION_NOARGS ();

	NS_ASSERT_MSG (buf != 0, "invalid buf argument");
	NS_ASSERT_MSG (len > 0, "invalid len argument");

	NS_LOG_INFO ("ContikiNetDevice::ReadCallback(): Received packet on node " << m_nodeId);
	NS_LOG_INFO ("ContikiNetDevice::ReadCallback(): Scheduling handler");
	Simulator::ScheduleWithContext (m_nodeId, Seconds (0.0), MakeEvent (&ContikiNetDevice::ForwardToBridgedDevice, this, buf, len));
}

void
ContikiNetDevice::ForwardToBridgedDevice (uint8_t *buf, ssize_t len)
{
	NS_LOG_FUNCTION (buf << len);

	//
	// First, create a packet out of the byte buffer we received and free that
	// buffer.
	//
	Ptr<Packet> packet = Create<Packet> (reinterpret_cast<const uint8_t *> (buf), len);
	free (buf);
	buf = 0;

	Address src, dst;
	uint16_t type;

	NS_LOG_LOGIC ("Received packet from socket");

	// Pull source, destination and type information from packet
	Ptr<Packet> p = Filter (packet, &src, &dst, &type);

	if (p == 0)
	{
		NS_LOG_LOGIC ("ContikiNetDevice::ForwardToBridgedDevice:  Discarding packet as unfit for ns-3 consumption");
		return;
	}

	NS_LOG_LOGIC ("Pkt source is " << src);
	NS_LOG_LOGIC ("Pkt destination is " << dst);
	NS_LOG_LOGIC ("Pkt LengthType is " << type);
	NS_LOG_LOGIC ("Forwarding packet from external socket to simulated network");

	if (m_mode == MACPHYOVERLAY)
	{
		if (m_ns3AddressRewritten == false)
		{
			//
			// Set the ns-3 device's mac address to the overlying container's
			// mac address
			//
			Mac48Address learnedMac = Mac48Address::ConvertFrom (src);
			NS_LOG_LOGIC ("Learned MacAddr is " << learnedMac << ": setting ns-3 device to use this address");
			m_bridgedDevice->SetAddress (Mac48Address::ConvertFrom (learnedMac));
			m_ns3AddressRewritten = true;
		}

		NS_LOG_LOGIC ("Forwarding packet to ns-3 device via Send()");
		m_bridgedDevice->Send (packet, dst, type);
		//m_bridgedDevice->SendFrom (packet, src, dst, type);
		return;
	}
	else {
		Address nullAddr = Address();
		m_bridgedDevice->Send(packet, nullAddr, uint16_t(0));
	}
}

Ptr<Packet>
ContikiNetDevice::Filter (Ptr<Packet> p, Address *src, Address *dst, uint16_t *type)
{
	NS_LOG_FUNCTION (p);
	/* Fill out src, dst and maybe type for the Send() function
	 *   This needs to be completed for MACOVERLAY mode to function - currently crashes because improper src/dst assigned */
	return p;
}

Ptr<NetDevice>
ContikiNetDevice::GetBridgedNetDevice (void)
{
	NS_LOG_FUNCTION_NOARGS ();
	return m_bridgedDevice;
}

void
ContikiNetDevice::SetMac (Ptr<ContikiMac> mac)
{
	m_macLayer = mac;
}

Ptr<ContikiMac>
ContikiNetDevice::GetMac (void)
{
	return m_macLayer;
}

void
ContikiNetDevice::SetPhy (Ptr<ContikiPhy> phy)
{
	m_phy = phy;
}

Ptr<ContikiPhy>
ContikiNetDevice::GetPhy (void)
{
	return m_phy;
}

void
ContikiNetDevice::SetBridgedNetDevice (Ptr<NetDevice> bridgedDevice)
{
	NS_LOG_FUNCTION (bridgedDevice);

	NS_ASSERT_MSG (m_node != 0, "ContikiNetDevice::SetBridgedDevice:  Bridge not installed in a node");
	//NS_ASSERT_MSG (bridgedDevice != this, "ContikiNetDevice::SetBridgedDevice:  Cannot bridge to self");
	NS_ASSERT_MSG (m_bridgedDevice == 0, "ContikiNetDevice::SetBridgedDevice:  Already bridged");

	/* Disconnect the bridged device from the native ns-3 stack
	 *  and branch to network stack on the other side of the socket. */
	bridgedDevice->SetReceiveCallback (MakeCallback (&ContikiNetDevice::DiscardFromBridgedDevice, this));
	bridgedDevice->SetPromiscReceiveCallback (MakeCallback (&ContikiNetDevice::ReceiveFromBridgedDevice, this));
	m_bridgedDevice = bridgedDevice;
}

bool
ContikiNetDevice::DiscardFromBridgedDevice (Ptr<NetDevice> device, Ptr<const Packet> packet, uint16_t protocol, const Address &src)
{
	NS_LOG_FUNCTION (device << packet << protocol << src);
	NS_LOG_LOGIC ("Discarding packet stolen from bridged device " << device);
	return true;
}

bool
ContikiNetDevice::ReceiveFromBridgedDevice (Ptr<NetDevice> device, Ptr<const Packet> packet, uint16_t protocol, Address const &src, Address const &dst, PacketType packetType)
{
	NS_LOG_DEBUG ("Packet UID is " << packet->GetUid ());
	/* Forward packet to socket */
	Ptr<Packet> p = packet->Copy ();
	NS_LOG_LOGIC ("Writing packet to socket");
	p->CopyData (m_packetBuffer, p->GetSize ());

	if( sem_wait(m_sem_out) == -1)
		NS_FATAL_ERROR("sem_wait() failed: " << strerror(errno));

	void *retval = memcpy(m_traffic_out,m_packetBuffer,p->GetSize());

	if( sem_post(m_sem_out) == -1)
		NS_FATAL_ERROR("sem_wait() failed: " << strerror(errno));

	NS_ABORT_MSG_IF (retval == (void *) -1, "ContikiNetDevice::ReceiveFromBridgedDevice(): memcpy() (AKA Write) error.");
	//NS_LOG_UNCOND("NS3 -> Contiki: Wrote " << bytesWritten << " bytes to socket " << m_sock);
	NS_LOG_LOGIC ("End of receive packet handling on node " << m_node->GetId ());
	return true;
}

void
ContikiNetDevice::SetIfIndex (const uint32_t index)
{
	NS_LOG_FUNCTION_NOARGS ();
	m_ifIndex = index;
}

uint32_t
ContikiNetDevice::GetIfIndex (void) const
{
	NS_LOG_FUNCTION_NOARGS ();
	return m_ifIndex;
}

Ptr<Channel>
ContikiNetDevice::GetChannel (void) const
{
	NS_LOG_FUNCTION_NOARGS ();
	return 0;
}

void
ContikiNetDevice::SetAddress (Address address)
{
	NS_LOG_FUNCTION (address);
	m_address = Mac64Address::ConvertFrom (address);
}

Address
ContikiNetDevice::GetAddress (void) const
{
	NS_LOG_FUNCTION_NOARGS ();
	return m_address;
}

void
ContikiNetDevice::SetMode (std::string mode)
{
	NS_LOG_FUNCTION (mode);
	if (mode.compare("MACPHYOVERLAY") == 0) {
		m_mode = (ContikiNetDevice::Mode) 2;
	}
	else if (mode.compare("PHYOVERLAY") == 0) {
		m_mode = (ContikiNetDevice::Mode) 1;
	}
	else {
		m_mode = (ContikiNetDevice::Mode) 0;
	}
}

ContikiNetDevice::Mode
ContikiNetDevice::GetMode (void)
{
	NS_LOG_FUNCTION_NOARGS ();
	return m_mode;
}

bool
ContikiNetDevice::SetMtu (const uint16_t mtu)
{
	NS_LOG_FUNCTION_NOARGS ();
	m_mtu = mtu;
	return true;
}

uint16_t
ContikiNetDevice::GetMtu (void) const
{
	NS_LOG_FUNCTION_NOARGS ();
	return m_mtu;
}

bool
ContikiNetDevice::IsLinkUp (void) const
{
	NS_LOG_FUNCTION_NOARGS ();
	return true;
}

void
ContikiNetDevice::AddLinkChangeCallback (Callback<void> callback)
{
	NS_LOG_FUNCTION_NOARGS ();
}

bool
ContikiNetDevice::IsBroadcast (void) const
{
	NS_LOG_FUNCTION_NOARGS ();
	return true;
}

Address
ContikiNetDevice::GetBroadcast (void) const
{
	NS_LOG_FUNCTION_NOARGS ();
	return Mac64Address ("ff:ff:ff:ff:ff:ff:ff:ff");
}

bool
ContikiNetDevice::IsMulticast (void) const
{
	NS_LOG_FUNCTION_NOARGS ();
	return true;
}

Address
ContikiNetDevice::GetMulticast (Ipv4Address multicastGroup) const
{
	NS_LOG_FUNCTION (this << multicastGroup);
	Mac48Address multicast = Mac48Address::GetMulticast (multicastGroup);
	return multicast;
}

bool
ContikiNetDevice::IsPointToPoint (void) const
{
	NS_LOG_FUNCTION_NOARGS ();
	return false;
}

bool
ContikiNetDevice::IsBridge (void) const
{
	NS_LOG_FUNCTION_NOARGS ();
	//
	// Returning false from IsBridge in a device called ContikiNetDevice may seem odd
	// at first glance, but this test is for a device that bridges ns-3 devices
	// together.  The Tap bridge doesn't do that -- it bridges an ns-3 device to
	// a Linux device.  This is a completely different story.
	//
	return false;
}

bool
ContikiNetDevice::Send (Ptr<Packet> packet, const Address& dest, uint16_t protocolNumber)
{
	NS_LOG_FUNCTION (packet);
	/* Send to MAC Layer */
	m_macLayer->Enqueue(packet);
	return true;
}

bool
ContikiNetDevice::SendFrom (Ptr<Packet> packet, const Address& src, const Address& dst, uint16_t protocol)
{
	NS_LOG_FUNCTION (packet << src << dst << protocol);
	NS_FATAL_ERROR ("ContikiNetDevice::Send: You may not call SendFrom on a ContikiNetDevice directly");
	return true;
}

Ptr<Node>
ContikiNetDevice::GetNode (void) const
{
	NS_LOG_FUNCTION_NOARGS ();
	return m_node;
}

void
ContikiNetDevice::SetNode (Ptr<Node> node)
{
	NS_LOG_FUNCTION_NOARGS ();
	m_node = node;
}

bool
ContikiNetDevice::NeedsArp (void) const
{
	NS_LOG_FUNCTION_NOARGS ();
	return true;
}

void
ContikiNetDevice::SetReceiveCallback (NetDevice::ReceiveCallback cb)
{
	NS_LOG_FUNCTION_NOARGS ();
	m_rxCallback = cb;
}

void
ContikiNetDevice::SetPromiscReceiveCallback (NetDevice::PromiscReceiveCallback cb)
{
	NS_LOG_FUNCTION_NOARGS ();
	m_promiscRxCallback = cb;
}

bool
ContikiNetDevice::SupportsSendFrom () const
{
	NS_LOG_FUNCTION_NOARGS ();
	return true;
}

Address ContikiNetDevice::GetMulticast (Ipv6Address addr) const
{
	NS_LOG_FUNCTION (this << addr);
	return Mac48Address::GetMulticast (addr);
}

} // namespace ns3
