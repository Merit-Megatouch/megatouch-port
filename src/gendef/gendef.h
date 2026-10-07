// Declarations of the cabinet's gendef base classes (libgendef_common.so / libgendef_xml.so,
// loaded from the cabinet) and of the loader-only classes libmerit_gendef.so implements on top
// of them. Layouts and vtable order: docs/reference/gendef-records.md.
//
// Compiled with the pre-C++11 libstdc++ ABI (_GLIBCXX_USE_CXX11_ABI=0, see the Makefile):
// the cabinet libraries pass old-ABI std::string objects to the field functions.
#pragma once
#include <cstddef>
#include <string>
#include <vector>

class abstract_metadata;
class abstract_record;
struct db_common_index_def;

// stride 0xa8 (§1.4)
struct db_common_field_def {
    char name[0x32];
    char xml[0x36];
    char fmt[0xc];
    char type[0x24];
    void (*get)(abstract_record*, const char*);
    void (*put)(const abstract_record*, std::string*, bool);
    bool (*is_default)(const abstract_record*);
    bool attribute;
    bool put_flag;
    char pad[2];
};
static_assert(sizeof(db_common_field_def) == 0xa8, "db_common_field_def");

class abstract_metadata {                        // libgendef_common.so
public:
    abstract_metadata(const char* table, const db_common_field_def* fields, int n, const char* db,
                      const db_common_index_def* idx, int nidx, const char* const* triggers, int ntrig, bool skip_defaults);
private:
    unsigned char storage_[0x80];                // >= 0x48 in the cabinet library
};

class abstract_record {                          // libgendef_common.so
public:
    abstract_record();
    virtual ~abstract_record();
    virtual const abstract_metadata* Metadata() const;
    virtual abstract_record* clone() const = 0;
    virtual bool equal(const abstract_record*) const = 0;
    virtual bool not_equal(const abstract_record*) const = 0;
    virtual void copy(const abstract_record*) = 0;
    virtual void AdjustTime(int, long);
};

class abstract_xml_record : public abstract_record {   // libgendef_xml.so
public:
    abstract_xml_record();
    abstract_xml_record(const abstract_xml_record&);
    abstract_xml_record& operator=(const abstract_xml_record&);
    virtual ~abstract_xml_record();
    virtual const char* cTag() const = 0;
    virtual int eTag() const = 0;
    virtual bool AddChild(abstract_xml_record*, const char*, unsigned int);
    virtual abstract_xml_record* NewChild(char*);
    virtual std::vector<const abstract_xml_record*> GetChildren() const;
    virtual int ChildCount() const;
    virtual void AdjustTimeFields(int, long);
    virtual const char* ChildName(int) const;
protected:
    char* mLastXMLError;                         // +4
};
static_assert(sizeof(abstract_xml_record) == 8, "abstract_xml_record");
