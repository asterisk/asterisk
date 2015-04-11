"""add pjsip max_initial_qualify_time

Revision ID: 45119a33fbbe
Revises: 2256a84ca226
Create Date: 2015-04-10 12:29:43.077598

"""

# revision identifiers, used by Alembic.
revision = '45119a33fbbe'
down_revision = '2256a84ca226'

from alembic import op
import sqlalchemy as sa


def upgrade():
    op.add_column('ps_globals', sa.Column('max_initial_qualify_time', sa.Integer))

def downgrade():
    op.drop_column('ps_globals', 'max_initial_qualify_time')
