"""add bundle to ps_endpoints

Revision ID: de83fac997e2
Revises: 20abce6d1e3c
Create Date: 2017-10-24 17:10:57.242020

"""

# revision identifiers, used by Alembic.
revision = 'de83fac997e2'
down_revision = '20abce6d1e3c'

from alembic import op
import sqlalchemy as sa
from sqlalchemy.dialects.postgresql import ENUM

YESNO_NAME = 'yesno_values'
YESNO_VALUES = ['yes', 'no']

def upgrade():
     ############################# Enums ##############################

    # yesno_values have already been created, so use postgres enum object
    # type to get around "already created" issue - works okay with mysql
    yesno_values = ENUM(*YESNO_VALUES, name=YESNO_NAME, create_type=False)

    op.add_column('ps_endpoints', sa.Column('bundle', yesno_values))

def downgrade():
    context = op.get_context()

    if context.bind.dialect.name == 'mssql':
        op.drop_constraint('ck_ps_endpoints_bundle_yesno_values', 'ps_endpoints')
    op.drop_column('ps_endpoints', 'bundle')
