/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2007 INRIA, Mathieu Lacage
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
 * Authors: Mathieu Lacage <mathieu.lacage@gmail.com>
 */
#ifndef OBJECT_VECTOR_H
#define OBJECT_VECTOR_H

#include <vector>
#include "object.h"
#include "ptr.h"
#include "attribute.h"
#include "object-ptr-vector.h"

namespace ns3 {

typedef ObjectPtrVectorValue ObjectVectorValue;

template <typename T, typename U>
Ptr<const AttributeAccessor>
MakeObjectVectorAccessor (U T::*memberContainer);

template <typename T>
Ptr<const AttributeChecker> MakeObjectVectorChecker (void);

template <typename T, typename U, typename INDEX>
Ptr<const AttributeAccessor>
MakeObjectVectorAccessor (Ptr<U> (T::*get)(INDEX) const,
                          INDEX (T::*getN)(void) const);

template <typename T, typename U, typename INDEX>
Ptr<const AttributeAccessor>
MakeObjectVectorAccessor (INDEX (T::*getN)(void) const,
                          Ptr<U> (T::*get)(INDEX) const);

} // namespace ns3

namespace ns3 {

template <typename T, typename U>
Ptr<const AttributeAccessor>
MakeObjectVectorAccessor (U T::*memberVector)
{
  struct MemberStdContainer : public ObjectPtrVectorAccessor
  {
    virtual bool DoGetN (const ObjectBase *object, uint32_t *n) const {
      const T *obj = dynamic_cast<const T *> (object);
      if (obj == 0)
        {
          return false;
        }
      *n = (obj->*m_memberVector).size ();
      return true;
    }
    virtual Ptr<Object> DoGet (const ObjectBase *object, uint32_t i) const {
      const T *obj = static_cast<const T *> (object);
      typename U::const_iterator begin = (obj->*m_memberVector).begin ();
      typename U::const_iterator end = (obj->*m_memberVector).end ();
      uint32_t k = 0;
      for (typename U::const_iterator j = begin; j != end; j++, k++)
        {
          if (k == i)
            {
              return *j;
              break;
            }
        }
      NS_ASSERT (false);
      // quiet compiler.
      return 0;
    }
    U T::*m_memberVector;
  } *spec = new MemberStdContainer ();
  spec->m_memberVector = memberVector;
  return Ptr<const AttributeAccessor> (spec, false);
}

template <typename T>
Ptr<const AttributeChecker> MakeObjectVectorChecker (void)
{
  return MakeObjectPtrVectorChecker<T> ();
}

template <typename T, typename U, typename INDEX>
Ptr<const AttributeAccessor>
MakeObjectVectorAccessor (Ptr<U> (T::*get)(INDEX) const,
			  INDEX (T::*getN)(void) const)
{
  return MakeObjectPtrVectorAccessor<T,U,INDEX>(get, getN);
}

template <typename T, typename U, typename INDEX>
Ptr<const AttributeAccessor>
MakeObjectVectorAccessor (INDEX (T::*getN)(void) const,
			  Ptr<U> (T::*get)(INDEX) const)
{
  return MakeObjectPtrVectorAccessor<T,U,INDEX>(get, getN);
}



} // namespace ns3

#endif /* OBJECT_VECTOR_H */
