#include <semaphore.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string>

#include<sys/ipc.h>
#include<sys/shm.h>
#include <semaphore.h>

#include "contiki-device.h"

extern "C" void ContikiMain(char *node_id, int mode, const char *addr,
		char *app, semaphores_t *sharedSemaphores);

NS_LOG_COMPONENT_DEFINE("ContikiNetDevice");

namespace ns3 {
//Values for Semaphores

uint32_t ContikiNetDevice::m_nNodes;

/**
 * semaphores for Contiki controll
 */
semaphores_t *sharedSemaphores;

NS_OBJECT_ENSURE_REGISTERED(ContikiNetDevice);

TypeId ContikiNetDevice::GetTypeId(void) {
	static TypeId tid =
			TypeId("ns3::ContikiNetDevice").SetParent<NetDevice>().AddConstructor<
					ContikiNetDevice>().AddAttribute("Start",
					"The simulation time at which to spin up the socket device read thread.",
					TimeValue(Seconds(0.)),
					MakeTimeAccessor(&ContikiNetDevice::m_tStart),
					MakeTimeChecker()).AddAttribute("Stop",
					"The simulation time at which to tear down the socket device read thread.",
					TimeValue(Seconds(0.)),
					MakeTimeAccessor(&ContikiNetDevice::m_tStop),
					MakeTimeChecker());
	return tid;
}

ContikiNetDevice::ContikiNetDevice() :
		m_node(0), m_ifIndex(0), m_startEvent(), m_stopEvent(), m_ipcReader(
				0) {
	NS_LOG_FUNCTION_NOARGS ();

	Start(m_tStart);
}

ContikiNetDevice::~ContikiNetDevice() {
	NS_LOG_FUNCTION_NOARGS ();

	StopContikiDevice();

	delete[] m_packetBuffer;
	m_packetBuffer = 0;
	m_bridgedDevice = 0;
}

uint32_t ContikiNetDevice::GetNNodes(void) {
	NS_LOG_FUNCTION_NOARGS ();
	return m_nNodes;
}

void ContikiNetDevice::SetNNodes(uint32_t n) {
	NS_LOG_FUNCTION (n);
	m_nNodes = n;
}
void ContikiNetDevice::DoDispose() {
	NS_LOG_FUNCTION_NOARGS ();
	StopContikiDevice();
	m_macLayer->Dispose ();
	m_phy->Dispose ();
	m_macLayer = 0;
	m_phy = 0;
	NetDevice::DoDispose();
}

void ContikiNetDevice::Start(Time tStart) {
	NS_LOG_FUNCTION (tStart);

	//
	//
	// Cancel any pending start event and schedule a new one at some relative time in the future.
	//
	Simulator::Cancel(m_startEvent);
	m_startEvent = Simulator::Schedule(tStart,
			&ContikiNetDevice::StartContikiDevice, this);

	void (*f)(void) = 0;
	Simulator::ScheduleWithContext(m_nodeId, NanoSeconds(1.0), f);
}

void ContikiNetDevice::Stop(Time tStop) {
	NS_LOG_FUNCTION (tStop);
	//
	// Cancel any pending stop event and schedule a new one at some relative time in the future.
	//
	Simulator::Cancel(m_stopEvent);
	m_stopEvent = Simulator::Schedule(tStop,
			&ContikiNetDevice::StopContikiDevice, this);

	// Special case
	sem_destroy(&sharedSemaphores->sem_time);
	shmdt(&sharedSemaphores->shm_time);
	shmctl(sharedSemaphores->shm_time_id, IPC_RMID, 0);
	sharedSemaphores->shm_time_id = -1;
}


void ContikiNetDevice::StartContikiDevice(void) {
	NS_LOG_FUNCTION_NOARGS ();

	m_nodeId = GetNode()->GetId();

	// Launching contiki process and wait

	NS_LOG_LOGIC ("Creating IPC Shared Memory");

	//
	// Spin up the Contiki Device and start receiving packets.
	//
	//
	// Now spin up a read thread to read packets from the tap device.
	//
	NS_ABORT_MSG_IF(m_ipcReader != 0,
			"ContikiNetDevice::StartContikiDevice(): Receive thread is already running");
	NS_LOG_LOGIC ("Spinning up read thread");

	m_ipcReader = Create<IpcReader>();
	sharedSemaphores = m_ipcReader->Start(MakeCallback(&ContikiNetDevice::ReadCallback, this),
			m_nodeId, child);

	if ((child = fork()) == -1)
		NS_ABORT_MSG(
				"ContikiNetDevice::CreateIpc(): Unix fork error, errno = " << strerror (errno));
	else if (child) { /*  This is the parent. */
		NS_LOG_DEBUG ("Parent process"); NS_LOG_LOGIC("Child PID: " << child);

	} else {

		Ptr<NetDevice> nd = GetBridgedNetDevice();
		Ptr<Node> n = nd->GetNode();

		/* Generate MAC address, assign to Node */
		uint8_t address[8];

		uint64_t id = (uint64_t) m_nodeId + 1;

		address[0] = (id >> 56) & 0xff;
		address[1] = (id >> 48) & 0xff;
		address[2] = (id >> 40) & 0xff;
		address[3] = (id >> 32) & 0xff;
		address[4] = (id >> 24) & 0xff;
		address[5] = (id >> 16) & 0xff;
		address[6] = (id >> 8) & 0xff;
		address[7] = (id >> 0) & 0xff;

		Mac64Address mac64Address;
		mac64Address.CopyFrom(address);

		NS_LOG_LOGIC("Allocated Mac64Address " << mac64Address << "\n");

		Address ndAddress = Address(mac64Address);
		nd->SetAddress(ndAddress);

		std::stringstream ss;
		ss << m_nodeId;
		char c_nodeId[128];
		strcpy(c_nodeId, ss.str().c_str());
		char app[128] = "\0";
		strcpy(app, m_application.c_str());

		//char path[128] = "/home/kentux/mercurial/contiki-original/examples/dummy/dummy.ns3";

		//execlp(path,path,c_nodeId, "0","NULL", app, NULL);
		NS_LOG_LOGIC("ContikiMain pid " << getpid() << " nodeId " << c_nodeId << " m_nodeId " << m_nodeId);
		fflush (stdout);

		std::ostringstream nodeAddr;
		nodeAddr << mac64Address;

		ContikiMain(c_nodeId, 0, nodeAddr.str().c_str(), app, sharedSemaphores);

	}

}
uint64_t now =0;
void ContikiNetDevice::ContikiClockHandle(uint64_t oldValue,
		uint64_t newValue) {

	// This is because  Contiki's time granularity is Miliseconds and for ns-3
	// it is in nanoseconds
	now = newValue / 1000000;

	/////// Writing new time //////////////

	NS_LOG_LOGIC("Handling new time step " << newValue);
	std::cout
			<< " ---Wait Device  sharedSemaphores->sem_time ContikiClockHandle at "
			<< now << " \n"; // << GetNodeId() << m_nodeId<< " \n";
	if (sem_wait(&IpcReader::m_sem_time) == -1)
		NS_FATAL_ERROR("sem_wait() failed: " << strerror(errno));
	std::cout
			<< " ---Wait Device  sharedSemaphores->sem_time ContikiClockHandle at "
			<< now << " \n"; // << GetNodeId()m_nodeId<< " \n";

	// sets time
	memcpy(IpcReader::m_shm_time, (void *) &now, 8);

	///////////////////////////////////////
	std::cout
			<< " ---Post Device  sharedSemaphores->sem_time ContikiClockHandle at "
			<< now << " \n"; // << GetNodeId() << m_nodeId<< " \n";
	if (sem_post(&IpcReader::m_sem_time) == -1)
		perror("contiki sem_post(sem_time) error");
	std::cout
			<< " ---Post Device  sharedSemaphores->sem_time ContikiClockHandle at "
			<< now << " \n"; // << GetNodeId() << m_nodeId<< " \n";

	////// Waiting for contiki to live the moment /////////
	NS_LOG_LOGIC("ns-3 waiting for contikis at " << newValue << " milliseconds"
			<< std::endl);

	// Sinchronization mechanism:
	// posts a sem go for each contiki node that has things to do now (post sem_go)
	// waits for each one of these nodes to finnish their quantum of time (waits sem_done)
	std::list < uint32_t > listToWake = IpcReader::getReleaseSchedule(
			NanoSeconds(newValue));

//    std::cout << " --- #@#@#@ Post Device  listOfGoSemaphores[0] ContikiClockHandle at "<<listOfGoSemaphores[0]<< " \n";// << GetNodeId() << m_nodeId<< " \n";
//    std::cout << " --- #@#@#@ Post Device  listOfGoSemaphores[1] ContikiClockHandle at "<<listOfGoSemaphores[1]<< " \n";// << GetNodeId() << m_nodeId<< " \n";
//    sem_post(listOfGoSemaphores[0]);
//    sem_post(listOfGoSemaphores[1]);
	// Awakes only the nodes that has things to do now
	for (std::list<uint32_t>::iterator it = listToWake.begin();
			it != listToWake.end(); it++) {
		uint32_t index = *it;
		NS_LOG_LOGIC("Handling new time step " << newValue);

		// wakes up the specific nodestruct
		std::cout
				<< " ---Post Device  listOfGoSemaphores[index] ContikiClockHandle at "
				<< index << " \n"; // << GetNodeId() << m_nodeId<< " \n";
		if (sem_post(IpcReader::listOfGoSemaphores[index]) == -1)
			NS_FATAL_ERROR("sem_wait() failed: " << strerror(errno));
		std::cout
				<< " ---Post Device  listOfGoSemaphores[index] ContikiClockHandle at "
				<< *it << "  " << IpcReader::listOfGoSemaphores[index]
				<< " Passed \n"; // << GetNodeId() << m_nodeId<< " \n";

//
//		int rtval;
//		sem_getvalue(sharedSemaphores->sem_time, &rtval);
//		NS_LOG_LOGIC("valor sem time" << rtval);
	}

	// Waits for the nodes to finnish their jobs
	for (std::list<uint32_t>::iterator it = listToWake.begin();
			it != listToWake.end(); it++) {
		NS_LOG_LOGIC("Handling new time step " << newValue);

		// Verify if the specific node has finnished
		std::cout
				<< " ---Wait Device  listOfDoneSemaphores[*it] ContikiClockHandle at "
				<< *it << " \n"; // << GetNodeId() << m_nodeId<< " \n";
		if (sem_wait(IpcReader::listOfDoneSemaphores[*it]) == -1)
			NS_FATAL_ERROR("sem_wait() failed: " << strerror(errno));
		std::cout
				<< " ---Wait Device  listOfDoneSemaphores[*it] ContikiClockHandle at "
				<< *it << " \n"; // << GetNodeId() << m_nodeId<< " \n";
	}

	//////////////////////////////////////

	//XXX This trick is to force ns-3 to go in slow motion so that
	//Contiki can follow

//	//ToDo: I don't think this is needed anymore, evaluate later
//	void (*f)(void) = 0;
//	Simulator::ScheduleWithContext(0, Seconds(1.0), f);

}

uint32_t ContikiNetDevice::GetNodeId() {
	return m_nodeId;
}

void ContikiNetDevice::StopContikiDevice(void) {
	NS_LOG_FUNCTION_NOARGS ();

	if (m_ipcReader != 0) {
		m_ipcReader->Stop();
		m_ipcReader = 0;
	}

	NS_LOG_LOGIC("Killing Child");
	kill(child, SIGKILL);

	usleep(100000);

//	ClearIpc();
}


void ContikiNetDevice::ReadCallback(uint8_t *buf, ssize_t len) {
	NS_LOG_FUNCTION_NOARGS ();

	NS_ASSERT_MSG(buf != 0, "invalid buf argument");
	NS_ASSERT_MSG(len > 0, "invalid len argument");

	NS_LOG_INFO ("ContikiNetDevice::ReadCallback(): Received packet on node " << m_nodeId);NS_LOG_INFO ("ContikiNetDevice::ReadCallback(): Scheduling handler");
	Simulator::ScheduleWithContext(m_nodeId, Seconds(0.0),
			MakeEvent(&ContikiNetDevice::ForwardToBridgedDevice, this, buf,
					len));
}

void ContikiNetDevice::ForwardToBridgedDevice(uint8_t *buf, ssize_t len) {
	NS_LOG_FUNCTION (buf << len);

	//
	// First, create a packet out of the byte buffer we received and free that
	// buffer.
	//
	Ptr<Packet> packet = Create<Packet>(reinterpret_cast<const uint8_t *>(buf),
			len);
//	Time t = Simulator::Now();

	Address src, dst;
	uint16_t type;

	NS_LOG_LOGIC ("Received packet from socket");

	// Pull source, destination and type information from packet
	Ptr<Packet> p = Filter(packet, &src, &dst, &type);

	if (p == 0) {
		NS_LOG_LOGIC ("ContikiNetDevice::ForwardToBridgedDevice:  Discarding packet as unfit for ns-3 consumption");
		return;
	}

	NS_LOG_LOGIC ("Pkt source is " << src);
	NS_LOG_LOGIC ("Pkt destination is " << dst);
	NS_LOG_LOGIC ("Pkt LengthType is " << type);
	NS_LOG_LOGIC ("Forwarding packet from external socket to simulated network");

	free(buf);
	buf = 0;

	if (m_mode == MACPHYOVERLAY) {
		if (m_ns3AddressRewritten == false) {
			//
			// Set the ns-3 device's mac address to the overlying container's
			// mac address
			//
			Mac48Address learnedMac = Mac48Address::ConvertFrom(src);
			NS_LOG_LOGIC ("Learned MacAddr is " << learnedMac << ": setting ns-3 device to use this address");
			m_bridgedDevice->SetAddress(Mac48Address::ConvertFrom(learnedMac));
			m_ns3AddressRewritten = true;
		}

		NS_LOG_LOGIC ("Forwarding packet to ns-3 device via Send()");
		m_bridgedDevice->Send(packet, dst, type);
		//m_bridgedDevice->SendFrom (packet, src, dst, type);
		return;
	} else {
		Address nullAddr = Address();
		m_bridgedDevice->Send(packet, nullAddr, uint16_t(0));
	}


}

Ptr<Packet> ContikiNetDevice::Filter(Ptr<Packet> p, Address *src, Address *dst,
		uint16_t *type) {
	NS_LOG_FUNCTION (p);
	/* Fill out src, dst and maybe type for the Send() function
	 *   This needs to be completed for MACOVERLAY mode to function - currently crashes because improper src/dst assigned */
	return p;
}

Ptr<NetDevice> ContikiNetDevice::GetBridgedNetDevice(void) {
	NS_LOG_FUNCTION_NOARGS ();
	return m_bridgedDevice;
}

void ContikiNetDevice::SetMac(Ptr<ContikiMac> mac) {
	m_macLayer = mac;
}

Ptr<ContikiMac> ContikiNetDevice::GetMac(void) {
	return m_macLayer;
}

void ContikiNetDevice::SetPhy(Ptr<ContikiPhy> phy) {
	m_phy = phy;
}

Ptr<ContikiPhy> ContikiNetDevice::GetPhy(void) {
	return m_phy;
}

void ContikiNetDevice::SetBridgedNetDevice(Ptr<NetDevice> bridgedDevice) {
	NS_LOG_FUNCTION (bridgedDevice);

	NS_ASSERT_MSG(m_node != 0,
			"ContikiNetDevice::SetBridgedDevice:  Bridge not installed in a node");
	//NS_ASSERT_MSG (bridgedDevice != this, "ContikiNetDevice::SetBridgedDevice:  Cannot bridge to self");
	NS_ASSERT_MSG(m_bridgedDevice == 0,
			"ContikiNetDevice::SetBridgedDevice:  Already bridged");

	/* Disconnect the bridged device from the native ns-3 stack
	 *  and branch to network stack on the other side of the socket. */
	bridgedDevice->SetReceiveCallback(
			MakeCallback(&ContikiNetDevice::DiscardFromBridgedDevice, this));
	bridgedDevice->SetPromiscReceiveCallback(
			MakeCallback(&ContikiNetDevice::ReceiveFromBridgedDevice, this));
	m_bridgedDevice = bridgedDevice;
}

bool ContikiNetDevice::DiscardFromBridgedDevice(Ptr<NetDevice> device,
		Ptr<const Packet> packet, uint16_t protocol, const Address &src) {
	NS_LOG_FUNCTION (device << packet << protocol << src);NS_LOG_LOGIC ("Discarding packet stolen from bridged device " << device);
	return true;
}

bool ContikiNetDevice::ReceiveFromBridgedDevice(Ptr<NetDevice> device,
		Ptr<const Packet> packet, uint16_t protocol, Address const &src,
		Address const &dst, PacketType packetType) {
	NS_LOG_DEBUG ("Packet UID is " << packet->GetUid ());
	/* Forward packet to socket */
	Ptr<Packet> p = packet->Copy();
	NS_LOG_LOGIC ("Writing packet to shared memory");

	m_packetBuffer = new uint8_t[IpcReader::m_traffic_size];
	p->CopyData(m_packetBuffer, p->GetSize());

	NS_LOG_LOGIC("NS-3 is writing for node " << child << "\n");

	NS_LOG_LOGIC( " ---Wait Device  sharedSemaphores->sem_out ReceiveFromBridgedDevice at "
			<< m_nodeId << " \n");
	if (sem_wait(&sharedSemaphores->sem_out) == -1)
		NS_FATAL_ERROR("sem_wait() failed: " << strerror(errno));
	NS_LOG_LOGIC(" ---Wait Device  sharedSemaphores->sem_out ReceiveFromBridgedDevice at "
			<< m_nodeId << " \n");

	//TODO: Maybe check if p->GetSize() doesn't exceed m_traffic_size

	NS_LOG_LOGIC("NS-3 got sem_out for node " << child << "\n");

	size_t output_size = (size_t) p->GetSize();
	//writing traffic size first
	memcpy(sharedSemaphores->traffic_out, &output_size, sizeof(size_t));
	NS_LOG_LOGIC(" Send msg to contiki size:  " << output_size << "\n");

	//Now writing actual traffic
	void *retval = memcpy(sharedSemaphores->traffic_out + sizeof(size_t), m_packetBuffer,
			p->GetSize());

	std::cout
			<< " ---Post Device  sharedSemaphores->sem_out ReceiveFromBridgedDevice at "
			<< m_nodeId << " \n";
	if (sem_post(&sharedSemaphores->sem_out) == -1)
		NS_FATAL_ERROR("sem_wait() failed: " << strerror(errno));
	std::cout
			<< " ---Post Device  sharedSemaphores->sem_out ReceiveFromBridgedDevice at "
			<< m_nodeId << " \n";

	NS_LOG_LOGIC("NS-3 wrote for node " << child << "\n");

	NS_ABORT_MSG_IF(retval == (void * ) -1,
			"ContikiNetDevice::ReceiveFromBridgedDevice(): memcpy() (AKA Write) error.");
	NS_LOG_LOGIC ("End of receive packet handling on node " << m_node->GetId ());

	delete[] m_packetBuffer;

	// registers the receiving packet event
//	Time t = Simulator::Now();
	m_ipcReader->SetRelativeTimer(); //TODO: See the consideration of the channel delay

	return true;
}

void ContikiNetDevice::SetIfIndex(const uint32_t index) {
	NS_LOG_FUNCTION_NOARGS ();
	m_ifIndex = index;
}

uint32_t ContikiNetDevice::GetIfIndex(void) const {
	NS_LOG_FUNCTION_NOARGS ();
	return m_ifIndex;
}

Ptr<Channel> ContikiNetDevice::GetChannel(void) const {
	NS_LOG_FUNCTION_NOARGS ();
	return 0;
}

void ContikiNetDevice::SetAddress(Address address) {
	NS_LOG_FUNCTION (address);
	m_address = Mac64Address::ConvertFrom(address);
}

Address ContikiNetDevice::GetAddress(void) const {
	NS_LOG_FUNCTION_NOARGS ();
	return m_address;
}

void ContikiNetDevice::SetMode(std::string mode) {
	NS_LOG_FUNCTION (mode);
	if (mode.compare("MACPHYOVERLAY") == 0) {
		m_mode = (ContikiNetDevice::Mode) 2;
	} else if (mode.compare("PHYOVERLAY") == 0) {
		m_mode = (ContikiNetDevice::Mode) 1;
	} else {
		m_mode = (ContikiNetDevice::Mode) 0;
	}
}

void ContikiNetDevice::SetApplication(std::string application) {

	m_application = application;
}

ContikiNetDevice::Mode ContikiNetDevice::GetMode(void) {
	NS_LOG_FUNCTION_NOARGS ();
	return m_mode;
}

bool ContikiNetDevice::SetMtu(const uint16_t mtu) {
	NS_LOG_FUNCTION_NOARGS ();
	m_mtu = mtu;
	return true;
}

uint16_t ContikiNetDevice::GetMtu(void) const {
	NS_LOG_FUNCTION_NOARGS ();
	return m_mtu;
}

bool ContikiNetDevice::IsLinkUp(void) const {
	NS_LOG_FUNCTION_NOARGS ();
	return true;
}

void ContikiNetDevice::AddLinkChangeCallback(Callback<void> callback) {
	NS_LOG_FUNCTION_NOARGS ();
}

bool ContikiNetDevice::IsBroadcast(void) const {
	NS_LOG_FUNCTION_NOARGS ();
	return true;
}

Address ContikiNetDevice::GetBroadcast(void) const {
	NS_LOG_FUNCTION_NOARGS ();
	return Mac64Address("ff:ff:ff:ff:ff:ff:ff:ff");
}

bool ContikiNetDevice::IsMulticast(void) const {
	NS_LOG_FUNCTION_NOARGS ();
	return true;
}

Address ContikiNetDevice::GetMulticast(Ipv4Address multicastGroup) const {
	NS_LOG_FUNCTION (this << multicastGroup);
	Mac48Address multicast = Mac48Address::GetMulticast(multicastGroup);
	return multicast;
}

bool ContikiNetDevice::IsPointToPoint(void) const {
	NS_LOG_FUNCTION_NOARGS ();
	return false;
}

bool ContikiNetDevice::IsBridge(void) const {
	NS_LOG_FUNCTION_NOARGS ();
	//
	// Returning false from IsBridge in a device called ContikiNetDevice may seem odd
	// at first glance, but this test is for a device that bridges ns-3 devices
	// together.  The Tap bridge doesn't do that -- it bridges an ns-3 device to
	// a Linux device.  This is a completely different story.
	//
	return false;
}

bool ContikiNetDevice::Send(Ptr<Packet> packet, const Address& dest,
		uint16_t protocolNumber) {
	NS_LOG_FUNCTION (packet);
	/* Send to MAC Layer */
	m_macLayer->Enqueue(packet);
	return true;
}

bool ContikiNetDevice::SendFrom(Ptr<Packet> packet, const Address& src,
		const Address& dst, uint16_t protocol) {
	NS_LOG_FUNCTION (packet << src << dst << protocol);
	NS_FATAL_ERROR(
			"ContikiNetDevice::Send: You may not call SendFrom on a ContikiNetDevice directly");
	return true;
}

Ptr<Node> ContikiNetDevice::GetNode(void) const {
	NS_LOG_FUNCTION_NOARGS ();
	return m_node;
}

void ContikiNetDevice::SetNode(Ptr<Node> node) {
	NS_LOG_FUNCTION_NOARGS ();
	m_node = node;
}

bool ContikiNetDevice::NeedsArp(void) const {
	NS_LOG_FUNCTION_NOARGS ();
	return true;
}

void ContikiNetDevice::SetReceiveCallback(NetDevice::ReceiveCallback cb) {
	NS_LOG_FUNCTION_NOARGS ();
	m_rxCallback = cb;
}

void ContikiNetDevice::SetPromiscReceiveCallback(
		NetDevice::PromiscReceiveCallback cb) {
	NS_LOG_FUNCTION_NOARGS ();
	m_promiscRxCallback = cb;
}

bool ContikiNetDevice::SupportsSendFrom() const {
	NS_LOG_FUNCTION_NOARGS ();
	return true;
}

Address ContikiNetDevice::GetMulticast(Ipv6Address addr) const {
	NS_LOG_FUNCTION (this << addr);
	return Mac48Address::GetMulticast(addr);
}

} // namespace ns3
