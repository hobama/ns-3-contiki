/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#include <stdio.h>
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"
#include "ns3/wifi-module.h"
#include "ns3/contiki-device.h"
#include "ns3/contiki-device-helper.h"
#include "ns3/propagation-loss-model.h"
#include "ns3/propagation-delay-model.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("ContikiDeviceExample");

void callback(void)
{
	Time now = Simulator::Now();
	NS_LOG_UNCOND("Now is " << now.GetSeconds());
}
int 
main (int argc, char *argv[])
{
  //GlobalValue::Bind ("SimulatorImplementationType", StringValue ("ns3::RealtimeSimulatorImpl"));
  //GlobalValue::Bind ("ChecksumEnabled", BooleanValue (true));
  //LogComponentEnable("DefaultSimulatorImpl", LOG_LEVEL_LOGIC);


	CommandLine cmd;

	uint32_t nNodes = 1;
	uint32_t sTime = 60; // default 60 seconds

	std::string apps = "udp-server.so,udp-client.so";

	cmd.AddValue("nNodes", "Number of nodes", nNodes);
	cmd.AddValue("sTime", "Simulation time", sTime);
	cmd.AddValue("apps", "Comma separated list of contiki applications", apps);

	cmd.Parse (argc,argv);
	if(nNodes < 1)
		nNodes = 1;

  /* Create nNodes nodes */
  NodeContainer contikiNodes;
  contikiNodes.Create(nNodes);
  NodeContainer wifiNodes;
  wifiNodes.Create(1);


  WifiHelper wifiHelper;

  Ptr<WifiNetDevice> wifiDev;
  /* Bridge nodes to Contiki processes */ 

  ContikiNetDeviceHelper contikiDeviceHelper;
  contikiDeviceHelper.Install(contikiNodes, "PHYOVERLAY", apps, true);


  Simulator::Stop (Seconds (sTime));
  Simulator::Run ();
  Simulator::Destroy ();
  
  return 0;
}
