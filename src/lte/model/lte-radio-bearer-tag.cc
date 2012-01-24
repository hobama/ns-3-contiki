/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2011 Centre Tecnologic de Telecomunicacions de Catalunya (CTTC)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Marco Miozzo <marco.miozzo@cttc.es>
 */


#include "lte-radio-bearer-tag.h"
#include "ns3/tag.h"
#include "ns3/uinteger.h"

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED (LteRadioBearerTag);

TypeId
LteRadioBearerTag::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::LteRadioBearerTag")
    .SetParent<Tag> ()
    .AddConstructor<LteRadioBearerTag> ()
    .AddAttribute ("rnti", "The rnti that indicates the UE which packet belongs",
                   UintegerValue (0),
                   MakeUintegerAccessor (&LteRadioBearerTag::GetRnti),
                   MakeUintegerChecker<uint16_t> ())
    .AddAttribute ("lcid", "The LC id that indicates the UE which packet belongs",
                   UintegerValue (0),
                   MakeUintegerAccessor (&LteRadioBearerTag::GetLcid),
                   MakeUintegerChecker<uint8_t> ())
  ;
  return tid;
}

TypeId
LteRadioBearerTag::GetInstanceTypeId (void) const
{
  return GetTypeId ();
}

LteRadioBearerTag::LteRadioBearerTag ()
  : m_rnti (0),
    m_lcid (0)
{
}
LteRadioBearerTag::LteRadioBearerTag (uint16_t rnti, uint8_t lcid)
  : m_rnti (rnti),
    m_lcid (lcid)
{
}

void
LteRadioBearerTag::SetRnti (uint16_t rnti)
{
  m_rnti = rnti;
}

void
LteRadioBearerTag::SetLcid (uint8_t lcid)
{
  m_lcid = lcid;
}

uint32_t
LteRadioBearerTag::GetSerializedSize (void) const
{
  return 3;
}

void
LteRadioBearerTag::Serialize (TagBuffer i) const
{
  i.WriteU16 (m_rnti);
  i.WriteU8 (m_lcid);
}

void
LteRadioBearerTag::Deserialize (TagBuffer i)
{
  m_rnti = (uint16_t) i.ReadU16 ();
  m_lcid = (uint8_t) i.ReadU8 ();
}

uint16_t
LteRadioBearerTag::GetRnti () const
{
  return m_rnti;
}

uint8_t
LteRadioBearerTag::GetLcid () const
{
  return m_lcid;
}

void
LteRadioBearerTag::Print (std::ostream &os) const
{
  os << "rnti=" << m_rnti << ", lcid=" << m_lcid;
}

} // namespace ns3
