#include "dyscostman.h"
#include "dyscodistribution.h"
#include "dysconormalization.h"
#include "stmanmodifier.h"
#include "stochasticencoder.h"
#include "stopwatch.h"
#include "weightencoder.h"

#include <casacore/ms/MeasurementSets/MeasurementSet.h>

#include <casacore/tables/Tables/ArrColDesc.h>
#include <casacore/tables/Tables/ArrayColumn.h>
#include <casacore/tables/Tables/ScalarColumn.h>

using namespace dyscostman;

template<typename T>
void createDyscoStManColumn(casacore::MeasurementSet& ms, const std::string& name, const casacore::IPosition& shape, unsigned bitsPerComplex, unsigned bitsPerWeight, DyscoNormalization normalization, DyscoDistribution distribution, double studentsTNu, double distributionTruncation)
{
	std::cout << "Constructing new column '" << name << "'...\n";
	casacore::ArrayColumnDesc<T> columnDesc(name, "", "DyscoStMan", "DyscoStMan", shape);
	columnDesc.setOptions(casacore::ColumnDesc::Direct | casacore::ColumnDesc::FixedShape);
	
	std::cout << "Querying storage manager...\n";
	bool isAlreadyUsed;
	try {
		ms.findDataManager("DyscoStMan");
		isAlreadyUsed = true;
	} catch(std::exception &e)
	{
		std::cout << "Constructing storage manager...\n";
		DyscoStMan dataManager(bitsPerComplex, bitsPerWeight);
		switch(distribution) {
			case GaussianDistribution:
				dataManager.SetGaussianDistribution();
				break;
			case UniformDistribution:
				dataManager.SetUniformDistribution();
				break;
			case StudentsTDistribution:
				dataManager.SetStudentsTDistribution(studentsTNu);
				break;
			case TruncatedGaussianDistribution:
				dataManager.SetTruncatedGaussianDistribution(distributionTruncation);
				break;
		}
		dataManager.SetNormalization(normalization);
		std::cout << "Adding column...\n";
		ms.addColumn(columnDesc, dataManager);
		isAlreadyUsed = false;
	}
	
	// We do not want to do this within the try-catch
	if(isAlreadyUsed)
	{
		std::cout << "Adding column with existing datamanager...\n";
		ms.addColumn(columnDesc, "DyscoStMan", false);
	}
}

/**
 * Compress a given measurement set.
 * @param argc Command line parameter count
 * @param argv Command line parameters.
 */
int main(int argc, char *argv[])
{
	register_dyscostman();
	
	if(argc < 2)
	{
		std::cout <<
			"Usage: dscompress [options] [-column <name> [-column...]] <ms>\n"
			"\n"
			"This tool replaces one or multiple columns of a measurement set by compressed columns, using the\n"
			"Dysco compression storage manager. This tools is mainly aimed at testing the technique.\n"
			"Better efficiency can be achieved by integrating the Dysco storage manager directly into\n"
			"a preprocessing pipeline or correlator output.\n"
			"\n"
			"The Dysco compression technique is explained in http://arxiv.org/abs/1609.02019.\n"
			"\n"
			"Options:\n"
			"-rfnormalization / -afnormalization / -rownormalization\n"
			"\tSelect normalization method. Default is AF normalization. For high bitrates, RF normalization\n"
			"\tis recommended. The use of row normalization is discouraged because it can be unstable.\n"
			"-data-bit-rate <n>\n"
			"\tSets the number of bits per float for visibility data. Because a visibility is a complex number,\n"
			"\tthe total nr bits per visibility will be twice this number. The compression rate is n/32.\n"
			"-weight-bit-rate <n>\n"
			"\tSets the number of bits per float for the data weights. The storage manager will use a single\n"
			"\tweight for all polarizations, hence with four polarizations the compression of weight is\n"
			"\t1/4 * n/32.\n"
			"-reorder\n"
			"\tWill rewrite the measurement set after replacing the column. This makes sure that the space\n"
			"\tof the old column is freed. It is for testing only, because the compression error is applied\n"
			"\ttwice to the data.\n"
			"-uniform / -gaussian / -truncgaus <sigma> / -studentt\n"
			"\tSelect the distribution used for the quantization of the data. The truncated gaussian and\n"
			"\tuniform distributions generally produce the most accurate results. The default is truncgaus\n"
			"\twith sigma=2.5, which is approximately optimal for bitrates 4-8.\n" 
			"\n"
			"Defaults: \n"
			"\tbits per data val = 8\n"
			"\tbits per weight = 12\n"
			"\tdistribution = TruncGaus with sigma=2.5\n"
			"\tnormalization = AF\n";
		return 0;
	}
	
	DyscoDistribution distribution = TruncatedGaussianDistribution;
	DyscoNormalization normalization = AFNormalization;
	bool reorder = false;
	unsigned bitsPerFloat=8, bitsPerWeight=12;
	double distributionTruncation = 2.5;
	
	std::vector<std::string> columnNames;
	
	int argi = 1;
	while(argi < argc && argv[argi][0]=='-')
	{
		std::string p(argv[argi]+1);
		if(p == "data-bit-rate")
		{
			++argi;
			bitsPerFloat = atoi(argv[argi]);
		}
		else if(p == "weight-bit-rate")
		{
			++argi;
			bitsPerWeight = atoi(argv[argi]);
		}
		else if(p == "reorder")
		{
			reorder = true;
		}
		else if(p == "gaussian")
		{
			distribution = GaussianDistribution;
		}
		else if(p == "uniform")
		{
			distribution = UniformDistribution;
		}
		else if(p == "studentt")
		{
			distribution = StudentsTDistribution;
		}
		else if(p == "truncgaus")
		{
			++argi;
			distribution = TruncatedGaussianDistribution;
			distributionTruncation = atof(argv[argi]);
		}
		else if(p == "column")
		{
			++argi;
			columnNames.push_back(argv[argi]);
		}
		else if(p == "rfnormalization")
		{
			normalization = RFNormalization;
		}
		else if(p == "afnormalization")
		{
			normalization = AFNormalization;
		}
		else if(p == "rownormalization")
		{
			normalization = RowNormalization;
		}
		else throw std::runtime_error(std::string("Invalid parameter: ") + argv[argi]);
		++argi;
	}
	
	if(columnNames.empty())
		columnNames.push_back("DATA");
	std::string msPath = argv[argi];

	std::cout <<
			"\tbits per data val = " << bitsPerFloat << "\n"
			"\tbits per weight = " << bitsPerWeight << "\n"
			"\tdistribution = ";
	switch(distribution)
	{
		case UniformDistribution:
			std::cout << "Uniform";
			break;
		case GaussianDistribution:
			std::cout << "Gaussian";
			break;
		case TruncatedGaussianDistribution:
			std::cout << "Truncated Gaussian with sigma=" << distributionTruncation;
			break;
		case StudentsTDistribution:
			std::cout << "Student T";
			break;
		default:
			std::cout << "?";
			break;
	}
	std::cout << 
		"\n"
		"\tnormalization = ";
	switch(normalization)
	{
		case AFNormalization:
			std::cout << "AF";
			break;
		case RFNormalization:
			std::cout << "RF";
			break;
		case RowNormalization:
			std::cout << "Row";
			break;
		default:
			std::cout << "?";
			break;
	}
	std::cout << "\n\n";
	
	std::cout << "Opening ms...\n";
	std::unique_ptr<casacore::MeasurementSet> ms(new casacore::MeasurementSet(msPath, casacore::Table::Update));
	
	Stopwatch watch(true);
	std::cout << "Replacing flagged values by NaNs...\n";
	for(std::string columnName : columnNames)
	{
		if(columnName != "WEIGHT_SPECTRUM")
		{
			casacore::ArrayColumn<std::complex<float>> dataCol(*ms, columnName);
			casacore::ArrayColumn<bool> flagCol(*ms, casacore::MeasurementSet::columnName(casacore::MSMainEnums::FLAG));
			casacore::Array<std::complex<float>> dataArr(dataCol.shape(0));
			casacore::Array<bool> flagArr(flagCol.shape(0));
			for(size_t row=0; row!=ms->nrow(); ++row)
			{
				dataCol.get(row, dataArr);
				flagCol.get(row, flagArr);
				casacore::Array<bool>::contiter f=flagArr.cbegin();
				bool isChanged = false;
				for(casacore::Array<std::complex<float>>::contiter d=dataArr.cbegin(); d!=dataArr.cend(); ++d)
				{
					if(*f) {
						*d = std::complex<float>(std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN());
						isChanged=true;
					}
					++f;
				}
				if(isChanged)
					dataCol.put(row, dataArr);
			}
		}
	}
	std::cout << "Time taken: " << watch.ToString() << '\n';
	watch.Reset();
	watch.Start();
	
	StManModifier modifier(*ms);
	
	casacore::IPosition shape;
	bool isDataReplaced = false;
	for(std::string columnName : columnNames)
	{
		bool replaced;
		if(columnName == "WEIGHT_SPECTRUM")
			replaced = modifier.PrepareReplacingColumn<float>(columnName, "DyscoStMan", bitsPerFloat, bitsPerWeight, shape);
		else
			replaced = modifier.PrepareReplacingColumn<casacore::Complex>(columnName, "DyscoStMan", bitsPerFloat, bitsPerWeight, shape);
		isDataReplaced = replaced || isDataReplaced;
	}
	
	if(isDataReplaced) {
		for(std::string columnName : columnNames)
		{
			if(columnName == "WEIGHT_SPECTRUM")
				createDyscoStManColumn<float>(*ms, columnName, shape, bitsPerFloat, bitsPerWeight, normalization, distribution, 1.0, distributionTruncation);
			else
				createDyscoStManColumn<casacore::Complex>(*ms, columnName, shape, bitsPerFloat, bitsPerWeight, normalization, distribution, 1.0, distributionTruncation);
		}
		for(std::string columnName : columnNames)
		{
			if(columnName == "WEIGHT_SPECTRUM")
				modifier.MoveColumnData<float>(columnName);
			else
				modifier.MoveColumnData<casacore::Complex>(columnName);
		}
		if(reorder)
		{
			StManModifier::Reorder(ms, msPath);
		}
	}
	
	std::cout << "Finished. Compression time: " << watch.ToString() << "\n";
	
	return 0;
}
