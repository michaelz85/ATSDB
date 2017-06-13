#ifndef QUANTITY_H
#define QUANTITY_H

#include "configurable.h"

class Unit;

class Quantity : public Configurable
{
public:
    Quantity(const std::string &class_id, const std::string &instance_id, Configurable *parent);
    /// @brief Destructor
    virtual ~Quantity();

    virtual void generateSubConfigurable (const std::string &class_id, const std::string &instance_id);

    void addUnit (const std::string &name, double factor, const std::string &definition);
    /// @brief Returns factor from one unit to another
    double getFactor (const std::string &unit_source, const std::string &unit_destination);

    const std::map <std::string, Unit*> &units () const { return units_; }

protected:
    std::map <std::string, Unit*> units_;


    virtual void checkSubConfigurables () {}
};

#endif // QUANTITY_H