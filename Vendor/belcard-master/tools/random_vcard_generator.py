#!/usr/bin/env python
# -*- coding: utf8 -*-

from barnum import gen_data
import codecs
import frogress
import random
from random import shuffle

gender_vcard_list = ['M', 'F', 'O', 'N', 'U']

def generate_vCard():
	gender_initial = gender_vcard_list[random.randint(0, 4)]
	gender = None
	if gender_initial == 'M':
		gender = 'Male'
	elif gender_initial == 'F':
		gender = 'Female'

	(first_name, last_name) = gen_data.create_name(gender=gender)
	adr = gen_data.create_street()
	zip, city, state = gen_data.create_city_state_zip()

	properties = []
	properties.append('FN:{} {}\r\n'.format(first_name, last_name))
	if random.randint(0, 1):
		properties.append('N:{};{};;;\r\n'.format(last_name, first_name))
	if random.randint(0, 1):
		properties.append('TEL:tel:{}\r\n'.format(gen_data.create_phone()))
	if random.randint(0, 1):
		properties.append('GENDER:{}\r\n'.format(gender_initial))
	if random.randint(0, 1):
		properties.append('EMAIL:{}\r\n'.format(gen_data.create_email(name=(first_name, last_name)).lower()))
	if random.randint(0, 1):
		properties.append('IMPP:sip:{}@{}\r\n'.format(first_name.lower(), 'sip.linphone.org'))
	if random.randint(0, 1):
		properties.append('ADR:;;{};{};{};{};\r\n'.format(adr, city, state, zip))
	if random.randint(0, 1):
		properties.append('NOTE:{}\r\n'.format(gen_data.create_sentence()))
	if random.randint(0, 1):
		properties.append('ORG:{}\r\n'.format(gen_data.create_company_name()))
	if random.randint(0, 1):
		properties.append('BDAY:{0:%Y%m%d}\r\n'.format(gen_data.create_birthday()))
	
	shuffle(properties)
	vCard = 'BEGIN:VCARD\r\n'
	vCard += 'VERSION:4.0\r\n'
	for property in properties:
		vCard += property
	vCard += 'END:VCARD\r\n'
	return vCard

def main():
	output = ''
	count = 1000
	widgets = [frogress.BarWidget, frogress.PercentageWidget, frogress.ProgressWidget('vCard ')]
	for i in frogress.bar(range(count), steps=count, widgets=widgets):
		output += generate_vCard()

	with codecs.open('output.vcf', 'w', 'utf-8') as f:
		f.write(output)

if __name__ == "__main__":
	main()