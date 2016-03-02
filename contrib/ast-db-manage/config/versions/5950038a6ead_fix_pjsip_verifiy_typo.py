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
    yesno_values = ENUM(*YESNO_VALUES, name=YESNO_NAME, create_type=False)
    with op.batch_alter_table('ps_transports') as batch_op:
        batch_op.alter_column('verifiy_server', type_=yesno_values,
                    new_column_name='verify_server')


def downgrade():
    yesno_values = ENUM(*YESNO_VALUES, name=YESNO_NAME, create_type=False)
    with op.batch_alter_table('ps_transports') as batch_op:
        batch_op.alter_column('verify_server', type_=yesno_values,
                    new_column_name='verifiy_server')
