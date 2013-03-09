#include <iostream>
#include <sstream>
#include <vector>
#include <gtest/gtest.h>

#include <boost/foreach.hpp>
#include <boost/serialization/string.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/serialization/array.hpp>
#include <boost/serialization/map.hpp>
#include <boost/serialization/shared_ptr.hpp>
#include <boost/archive/xml_oarchive.hpp>
#include <boost/type_traits/is_same.hpp>
#include <boost/numeric/conversion/bounds.hpp>

#include "boost/archive/mongo_oarchive.hpp"
#include "boost/archive/mongo_iarchive.hpp"

#include "test/Plain.h"
#include "test/Simple.h"
#include "test/Poly.h"

using namespace boost::archive;
using boost::serialization::make_nvp;

typedef ::testing::Types<
		bool,
		char,
		signed char,
		unsigned char,
		wchar_t,
#if __cplusplus >= 201103L
		char16_t,
		char32_t,
#endif
		short,
		unsigned short,
		int,
		unsigned int,
		long,
		unsigned long,
		long long,
		unsigned long long,
		float,
		long double,
		double
	> builtin_types;

template<typename T>
struct MongoBuiltin :
	public ::testing::Test
{
	typedef T type;
};

struct ExternEqual {};
bool operator== (ExternEqual const&, ExternEqual const&) { return false; }

TYPED_TEST_CASE(MongoBuiltin, builtin_types);

TYPED_TEST(MongoBuiltin, BaseTypes)
{
	typedef typename TestFixture::type type;
	{
		mongo::BSONObjBuilder builder;
		mongo_oarchive out(builder);

		type a, b;
		a = 42;
		out << make_nvp("value", a);

		mongo::BSONObj obj = builder.obj();
		ASSERT_TRUE(!obj.toString().empty());

		mongo_iarchive in(obj);
		in >> make_nvp("value", b);

		ASSERT_EQ(a, b);
	}

	// early abort for long double, it is not supported by mongo.
	// Therefore, the following test inevitably fails.
	if (boost::is_same<type, long double>::value)
		return;

	{
		using namespace boost::numeric;
		mongo::BSONObjBuilder builder;
		mongo_oarchive out(builder);

		type a[3];
		type b[3];
		a[0] = bounds<type>::highest();
		a[1] = bounds<type>::smallest();
		a[2] = bounds<type>::lowest();
		out << make_nvp("value", a);

		mongo::BSONObj obj = builder.obj();
		ASSERT_TRUE(!obj.toString().empty());

		mongo_iarchive in(obj);
		in >> make_nvp("value", b);

		for (size_t ii = 0; ii<3; ++ii) {
			EXPECT_EQ(a[ii], b[ii]);
		}
	}
}


TEST(MongoArchive, CustomType)
{
	const char name [] = { "myType" };
	Simple a, b;
	a.n = static_cast<Simple::Name>(42);
	a.str = "hihihi";

	mongo::BSONObjBuilder builder;
	mongo_oarchive out(builder);

	out << make_nvp(name, a);

	mongo::BSONObj o = builder.obj();
	int e = o.getField(name).embeddedObject().getField("enum").Int();
	//ASSERT_EQ("hello",  o.getField(name).embeddedObject().getField("ch").String());
	ASSERT_EQ("hihihi",  o.getField(name).embeddedObject().getField("str").String());

	ASSERT_EQ((int)a.n, e);

	mongo_iarchive in(o);
	in >> make_nvp(name, b);

	ASSERT_EQ(a, b);
}

TEST(MongoArchive, AllMembersRegressionTest)
{
	const char name [] = { "myArray" };
	mongo::BSONObjBuilder builder;
	mongo_oarchive mongo(builder);

	boost::array<int, 42> a;
	mongo << make_nvp(name, a);

	mongo::BSONObj o = builder.obj();
	mongo::BSONElement elem = o.getField(name).embeddedObject().getField("elems");
	ASSERT_EQ(static_cast<int>(a.size()+1), elem.embeddedObject().nFields());
}

TEST(MongoArchive, NotCompressableRegressionTest)
{
	const char name [] = { "myUncompressableVector" };
	mongo::BSONObjBuilder builder;
	mongo_oarchive mongo(builder);

	ASSERT_TRUE (boost::has_equal_to<Simple>::value);
	ASSERT_TRUE (boost::has_equal_to<ExternEqual>::value);
	ASSERT_FALSE(boost::has_equal_to<Plain>::value);
	ASSERT_FALSE(boost::archive::detail::is_compressable<Plain>::value);

	std::vector<Plain> a(42);
	mongo << make_nvp(name, a);
}

TEST(MongoArchive, Array)
{
	const char name [] = { "myArray" };
	typedef boost::array<int, 1024> type;
	type a, b;

	mongo::BSONObjBuilder builder;
	mongo_oarchive out(builder);

	size_t cnt = 0;
	BOOST_FOREACH(int& val, a)
	{
		val = cnt++;
	}

	out << make_nvp(name, a);

	mongo::BSONObj o = builder.obj();

	mongo_iarchive in(o);
	in >> make_nvp(name, b);

	ASSERT_EQ(a, b);
}

TEST(MongoArchive, SparseArray)
{
	const char name [] = { "myArray" };
	typedef boost::array<int, 1024> type;
	type a, b;

	mongo::BSONObjBuilder builder;
	mongo_oarchive out(builder, mongo_oarchive::sparse_array);

	// zero all entries
	BOOST_FOREACH(int& val, a)
	{
		val = 0;
	}

	// set some to individual values
	int start=10, range=10;
	for(int ii = start; ii<start+range; ++ii)
		a[ii] = ii;

	out << make_nvp(name, a);

	mongo::BSONObj o = builder.obj();

	mongo::BSONElement elem = o.getField(name).embeddedObject().getField("elems");
	ASSERT_EQ(range+1, elem.embeddedObject().nFields());

	mongo_iarchive in(o, mongo_iarchive::sparse_array);
	in >> make_nvp(name, b);

	ASSERT_EQ(a, b);
}

TEST(MongoArchive, Map)
{
	const char name [] = { "myMap" };
	typedef std::map<std::string, Simple> map_t;
	map_t x, y;

	x["one"].a = 42;
	x["one"].b = 23;
	x["two"].a = 5;
	x["three"].b = 1337;
	ASSERT_EQ(3u, x.size());
	ASSERT_NE(x, y);

	mongo::BSONObjBuilder builder;
	mongo_oarchive out(builder);

	out << make_nvp(name, x);

	mongo::BSONObj o = builder.obj();

	mongo_iarchive in(o);
	in >> make_nvp(name, y);

	ASSERT_EQ(3u, y.size());
	ASSERT_EQ(x, y);
}

TEST(MongoArchive, Abstract)
{
	const char name [] = { "myAbstractType" };
	boost::shared_ptr<Base> x(new Poly (42));
	boost::shared_ptr<Base> y;

	Poly const& xp = dynamic_cast<Poly const&>(*x);
	ASSERT_EQ(42, xp.member);

	mongo::BSONObjBuilder builder;
	mongo_oarchive out(builder);

	out << make_nvp(name, x);

	mongo::BSONObj o = builder.obj();

	mongo_iarchive in(o);
	in >> make_nvp(name, y);

	Poly const& yp = dynamic_cast<Poly const&>(*y);
	ASSERT_EQ(xp, yp);
}
