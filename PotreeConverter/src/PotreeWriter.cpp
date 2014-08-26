

#include <cmath>

#include <boost/filesystem.hpp>

#include "PotreeWriter.h"
#include "LASPointReader.h"
#include "BINPointReader.hpp"
#include "LASPointWriter.hpp"
#include "BINPointWriter.hpp"

namespace fs = boost::filesystem;


PotreeWriterNode::PotreeWriterNode(PotreeWriter* potreeWriter, string name, string path, AABB aabb, float spacing, int level, int maxLevel){
	this->name = name;
	this->path = path;
	this->aabb = aabb;
	this->spacing = spacing;
	this->level = level;
	this->maxLevel = maxLevel;
	this->potreeWriter = potreeWriter;
	this->grid = new SparseGrid(aabb, spacing);
	numAccepted = 0;
	lastAccepted = 0;
	addCalledSinceLastFlush = false;

	for(int i = 0; i < 8; i++){
		children[i] = NULL;
	}
}

PointReader *PotreeWriterNode::createReader(string path){
	PointReader *reader = NULL;
	OutputFormat outputFormat = this->potreeWriter->outputFormat;
	if(outputFormat == OutputFormat::LAS || outputFormat == OutputFormat::LAZ){
		reader = new LASPointReader(path);
	}else if(outputFormat == OutputFormat::BINARY){
		reader = new BINPointReader(path);
	}

	return reader;
}

PointWriter *PotreeWriterNode::createWriter(string path){
	PointWriter *writer = NULL;
	OutputFormat outputFormat = this->potreeWriter->outputFormat;
	if(outputFormat == OutputFormat::LAS || outputFormat == OutputFormat::LAZ){
		LASheader header;
		header.clean();
		header.number_of_point_records = numAccepted;
		header.point_data_format = 2;
		header.point_data_record_length = 26;
		//header.set_bounding_box(acceptedAABB.min.x, acceptedAABB.min.y, acceptedAABB.min.z, acceptedAABB.max.x, acceptedAABB.max.y, acceptedAABB.max.z);
		header.set_bounding_box(aabb.min.x, aabb.min.y, aabb.min.z, aabb.max.x, aabb.max.y, aabb.max.z);
		header.x_scale_factor = 0.01;
		header.y_scale_factor = 0.01;
		header.z_scale_factor = 0.01;
		header.x_offset = 0.0f;
		header.y_offset = 0.0f;
		header.z_offset = 0.0f;

		writer = new LASPointWriter(path, header);
	}else if(outputFormat == OutputFormat::BINARY){
		writer = new BINPointWriter(path);
	}

	return writer;

	
}

void PotreeWriterNode::loadFromDisk(){
	PointReader *reader = createReader(path + "/data/" + name + potreeWriter->getExtension());
	while(reader->readNextPoint()){
		Point p = reader->getPoint();
		grid->addWithoutCheck(Vector3<double>(p.x, p.y, p.z));
	}
	grid->numAccepted = numAccepted;
	reader->close();
	delete reader;
}

PotreeWriterNode *PotreeWriterNode::add(Point &point, int minLevel){
	if(level > maxLevel){
		return NULL;
	}

	if(level < minLevel){
		// pass forth
		int childIndex = nodeIndex(aabb, point);
		if(childIndex >= 0){
			PotreeWriterNode *child = children[childIndex];

			if(child == NULL && level < maxLevel){
				child = createChild(childIndex);
			}

			child->add(point, minLevel);
		}
	}else{
		add(point);
	}
}

PotreeWriterNode *PotreeWriterNode::createChild(int childIndex ){
	stringstream childName;
	childName << name << childIndex;
	AABB cAABB = childAABB(aabb, childIndex);
	PotreeWriterNode *child = new PotreeWriterNode(potreeWriter, childName.str(), path, cAABB, spacing / 2.0f, level+1, maxLevel);
	children[childIndex] = child;

	return child;
}

PotreeWriterNode *PotreeWriterNode::add(Point &point){
	addCalledSinceLastFlush = true;

	// load grid from disk to memory, if necessary
	if(grid->numAccepted != numAccepted){
		loadFromDisk();
	}

	bool accepted = grid->add(Vector3<double>(point.x, point.y, point.z));
	//float minGap = grid->add(Vector3<double>(point.x, point.y, point.z));
	//bool accepted = minGap > spacing;
	//int targetLevel = ceil(log((1/minGap) * spacing) / log(2));
	//
	//if(targetLevel > maxLevel){
	//	return NULL;
	//}

	if(accepted){
		cache.push_back(point);
		acceptedAABB.update(Vector3<double>(point.x, point.y, point.z));
		potreeWriter->numAccepted++;
		numAccepted++;

		return this;
	}else if(level < maxLevel){
		// try adding point to higher level

		int childIndex = nodeIndex(aabb, point);
		if(childIndex >= 0){
			PotreeWriterNode *child = children[childIndex];

			// create child node if not existent
			if(child == NULL){
				child = createChild(childIndex);
			}

			return child->add(point);
			//child->add(point, targetLevel);
		}
	}else{
		return NULL;
	}
}

void PotreeWriterNode::flush(){

	if(cache.size() > 0){
		 // move data file aside to temporary directory for reading
		string filepath = path + "/data/" + name + potreeWriter->getExtension();
		string temppath = path +"/temp/prepend" + potreeWriter->getExtension();
		if(fs::exists(filepath)){
			fs::rename(fs::path(filepath), fs::path(temppath));
		}
		

		PointWriter *writer = createWriter(path + "/data/" + name + potreeWriter->getExtension());
		if(fs::exists(temppath)){
			PointReader *reader = createReader(temppath);
			while(reader->readNextPoint()){
				writer->write(reader->getPoint());
			}
			reader->close();
			delete reader;
			fs::remove(temppath);
		}

		for(int i = 0; i < cache.size(); i++){
			writer->write(cache[i]);
		}
		writer->close();
		delete writer;

		cache = vector<Point>();
	}else if(cache.size() == 0 && grid->numAccepted > 0 && addCalledSinceLastFlush == false){
		delete grid;
		grid = new SparseGrid(aabb, spacing);
	}
	
	addCalledSinceLastFlush = false;

	for(int i = 0; i < 8; i++){
		if(children[i] != NULL){
			children[i]->flush();
		}
	}
}