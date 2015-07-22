"""Fix pjsip transports verify column

Revision ID: 5950038a6ead
Revises: d39508cb8d8
Create Date: 2014-09-15 06:21:35.047314

"""

# revision identifiers, used by Alembic.
revision = '5950038a6ead'
down_revision = 'd39508cb8d8'

from alembic import op
from sqlalchemy.dialects.postgresql import ENUM

YESNO_NAME = 'yesno_values'
YESNO_VALUES = ['yes', 'no']


def upgrade():

	currentcontext = op.get_context()
	if currentcontext.bind.dialect.name != 'oracle':
		yesno_values = ENUM(*YESNO_VALUES, name=YESNO_NAME, create_type=False)
		op.alter_column('ps_transports', 'verifiy_server', type_=yesno_values,
						new_column_name='verify_server')
	if currentcontext.bind.dialect.name == 'oracle':
		#it is already fixed in moment of creation for oracle 
		#op.alter_column('ps_transports', 'verifiy_server', new_column_name='verify_server')
		#op.alter_column('ps_transports', 'verify_server', type_=ENUM(*YESNO_VALUES, name='pstrYnNverifyserver'), existing_type=ENUM(*YESNO_VALUES, name='pstrYnNverifiy_server'))
		pass

def downgrade():
	currentcontext = op.get_context()
	if currentcontext.bind.dialect.name != 'oracle':
		yesno_values = ENUM(*YESNO_VALUES, name=YESNO_NAME, create_type=False)
		op.alter_column('ps_transports', 'verify_server', type_=yesno_values,
						new_column_name='verifiy_server')
	if currentcontext.bind.dialect.name == 'oracle':
		pass